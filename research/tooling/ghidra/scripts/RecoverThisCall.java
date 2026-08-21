import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

// Give a C++ method the calling convention its ABI already fixed, so `this` stops being a
// register the decompiler cannot name.
//
// An MSVC method takes `this` in ECX. When Ghidra does not know a function is __thiscall it
// leaves the convention unknown, and the decompiler writes the receiver as `in_ECX` -- the name
// it uses for a register read without ever being written. The cost lands on the virtual call
// graph: a dispatch through `*in_ECX` names no class, so the site cannot be joined to a vtable,
// and 2,491 of them sit unresolved. With the convention applied the same expression reads
// `*(int *)this`, and the existing resolution rule -- the receiver is `this`, so its class is
// the caller's own -- picks it up.
//
// Two pieces of evidence, and BOTH are required:
//
//   1. The function is a method: it sits in a class namespace, or a class vftable holds it.
//      Only a method has a `this` to pass.
//   2. ECX is read before it is written. That is what makes ECX an INPUT rather than a
//      scratch register, and it is the same fact the decompiler is reporting when it invents
//      `in_ECX`.
//
// One alone is not enough. A method that never touches ECX may have been inlined into or may
// take no receiver in the path examined; a non-method that reads ECX first is reading a value
// its caller happened to leave there. Neither is a __thiscall, and a wrong convention shifts
// every parameter -- worse than no convention at all.
//
// Args (key=value, space separated):
//   out=<path>    report file (required)
//   apply=1       write to the project (default: report only)
public class RecoverThisCall extends GhidraScript {

    /** How far into the body to look for the first use of ECX. A receiver is touched early;
        scanning further starts reading unrelated scratch use in a later block. */
    private static final int WINDOW = 24;

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
            printerr("RecoverThisCall: out=<path> is required");
            return;
        }
        boolean apply = "1".equals(a.get("apply"));

        Register ecx = currentProgram.getRegister("ECX");
        if (ecx == null) {
            printerr("RecoverThisCall: this architecture has no ECX");
            return;
        }

        // Every address a class vftable holds, so a method Ghidra never placed in a namespace
        // still counts as one. The walk is the cheap version of DumpVtables': bounded at the
        // next table label, which is the same trap both of those carry.
        Set<Long> inVtable = vtableTargets();

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName()
                + (apply ? "  [applying]" : "  [report only]")
                + "  " + inVtable.size() + " distinct vftable slot targets");

        List<Function> functions = new ArrayList<>();
        FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
        while (all.hasNext()) functions.add(all.next());

        int method = 0, reads = 0, both = 0, applied = 0, already = 0, failed = 0;
        for (Function function : functions) {
            if (monitor.isCancelled()) break;
            if (function.isThunk() || function.isExternal()) continue;
            String convention = function.getCallingConventionName();
            if (convention != null && convention.contains("thiscall")) { already++; continue; }

            boolean isMethod = isMethod(function, inVtable);
            boolean readsFirst = readsBeforeWriting(function, ecx);
            if (isMethod) method++;
            if (readsFirst) reads++;
            if (!isMethod || !readsFirst) continue;
            both++;
            out.printf("%s  %s%n", function.getEntryPoint(), function.getName(true));
            if (!apply) continue;
            try {
                function.setCallingConvention("__thiscall");
                applied++;
            } catch (Exception e) {
                failed++;
                out.println("    not applied: " + e.getMessage());
            }
        }

        String summary = method + " function(s) are methods, " + reads
                + " read ECX before writing it, " + both + " are both and "
                + (apply ? applied + " were given __thiscall" : "would be given __thiscall")
                + "; " + already + " already had it, " + failed + " failed";
        out.println("\n// " + summary);
        out.close();
        println("RecoverThisCall: " + currentProgram.getName() + " -> " + summary);
    }

    private boolean isMethod(Function function, Set<Long> inVtable) {
        if (inVtable.contains(function.getEntryPoint().getOffset())) return true;
        String parent = function.getParentNamespace() == null
                ? "" : function.getParentNamespace().getName();
        return !parent.isEmpty() && !"Global".equals(parent);
    }

    /** Whether ECX is an input: read somewhere in the opening window before anything writes it.

        Read literally off the instruction's own operand model rather than the decompiler's, so
        it costs nothing -- this runs over every function in the module. */
    private boolean readsBeforeWriting(Function function, Register ecx) {
        InstructionIterator instructions =
                currentProgram.getListing().getInstructions(function.getBody(), true);
        for (int step = 0; step < WINDOW && instructions.hasNext(); step++) {
            Instruction at = instructions.next();
            for (Object one : at.getInputObjects()) {
                if (one instanceof Register && overlaps((Register) one, ecx)) return true;
            }
            for (Object one : at.getResultObjects()) {
                if (one instanceof Register && overlaps((Register) one, ecx)) return false;
            }
        }
        return false;
    }

    /** CL and CX are ECX, and a write to one is a write to part of the receiver. */
    private boolean overlaps(Register one, Register ecx) {
        return one.equals(ecx) || ecx.equals(one.getParentRegister())
                || one.equals(ecx.getParentRegister());
    }

    /** Every function address any `vftable_*` label points a slot at. */
    private Set<Long> vtableTargets() {
        TreeSet<Long> tables = new TreeSet<>();
        SymbolIterator labels = currentProgram.getSymbolTable()
                .getSymbolIterator("vftable_*", true);
        while (labels.hasNext()) tables.add(labels.next().getAddress().getOffset());
        Set<Long> targets = new HashSet<>();
        for (Long table : tables) {
            Long next = tables.higher(table);
            long limit = next == null ? Long.MAX_VALUE : next;
            for (long entry = table; entry < limit; entry += 4) {
                long value;
                try {
                    value = currentProgram.getMemory().getInt(toAddr(entry)) & 0xFFFFFFFFL;
                } catch (Exception e) { break; }
                Address target;
                try { target = toAddr(value); } catch (Exception e) { break; }
                if (getMemoryBlock(target) == null || !getMemoryBlock(target).isExecute()) break;
                targets.add(value);
                // A slot usually holds a 5-byte `E9 rel32` incremental-link thunk rather than
                // the body, and it is the BODY whose convention matters.
                long jumped = followJump(value);
                if (jumped != 0) targets.add(jumped);
            }
        }
        return targets;
    }

    private long followJump(long at) {
        try {
            if ((getByte(toAddr(at)) & 0xFF) != 0xE9) return 0;
            long destination = at + 5 + getInt(toAddr(at + 1));
            return getMemoryBlock(toAddr(destination)) != null
                    && getMemoryBlock(toAddr(destination)).isExecute() ? destination : 0;
        } catch (Exception e) {
            return 0;
        }
    }
}
