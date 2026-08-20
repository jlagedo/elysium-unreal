import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// Resolves MSVC adjustor thunks: the `sub ecx,<n>; jmp <method>` stubs that sit in a
// secondary base's vftable so a method compiled against the complete object still receives
// the right `this`.
//
// Following a vftable slot's 5-byte jump identifies which class overrides which method. On a
// secondary base the slot holds an adjustor thunk instead, so the jump lands on a body
// compiled for a different `this` and the override is attributed to the wrong class. This
// names those slots for what they are.
//
// The yield is small and does not track the multiple-inheritance count: `DumpRtti` reports 192
// such classes in vampire.dll and 1556 in client.dll, but MSVC only emits a thunk where a
// secondary base's virtual is actually overridden, which is rare here. Expect single digits.
//
// A `sub ecx,<n>; jmp` pair is not by itself a thunk -- several CRT routines contain one in
// ordinary code -- so a match only counts when it stands outside any function and a data slot
// points at it.
//
// Each thunk is named `<method>_adj<displacement>` and given a plate comment naming the method
// it forwards to. The report also names the owning class by walking back to the nearest
// `vftable_` label, so run `DumpRtti` first for that column to be populated. The script owns
// the `_adj<n>` suffix it writes, clears it before writing again, and leaves any thunk that
// already carries a name of its own alone.
//
// Args (key=value, space separated):
//   out=<path>   output file
//   name=0       report only; rename nothing
public class DumpAdjustorThunks extends GhidraScript {

    private static final int MAX_VFTABLE_SLOTS = 1024;

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/adjustors.txt");
        boolean rename = !"0".equals(a.get("name"));

        List<Thunk> thunks = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isExecute() || !block.isInitialized()) continue;
            InstructionIterator instructions =
                    currentProgram.getListing().getInstructions(block.getStart(), true);
            for (Instruction instruction : instructions) {
                monitor.checkCancelled();
                if (instruction.getAddress().compareTo(block.getEnd()) > 0) break;
                Thunk thunk = match(instruction);
                if (thunk != null) thunks.add(thunk);
            }
        }

        // A `sub ecx,<n>; jmp` pair also occurs inside ordinary code -- several CRT routines
        // contain one -- so the byte shape alone is not the thunk. What makes it a thunk is
        // that a vftable slot points at it. Requiring that is what separates the two.
        thunks.removeIf(thunk -> !referencedFromData(thunk.entry));
        for (Thunk thunk : thunks) thunk.owners = owners(thunk.entry);

        if (rename) clearOwnNames();
        int renamed = 0;
        for (Thunk thunk : thunks) {
            monitor.checkCancelled();
            if (!rename) continue;
            Function function = getFunctionAt(thunk.entry);
            if (function == null) function = createFunction(thunk.entry, null);
            if (function == null || !isUnnamed(function)) continue;
            function.setName(thunk.target.getName() + "_adj" + thunk.displacement, SourceType.ANALYSIS);
            setPlateComment(thunk.entry, "adjustor thunk: this -= " + thunk.displacement
                    + "; -> " + thunk.target.getName() + " @ " + thunk.target.getEntryPoint());
            renamed++;
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName() + "  " + thunks.size() + " adjustor thunks, "
                + renamed + " named");
        if (thunks.isEmpty()) {
            out.println("// no vftable slot in this module holds an adjustor thunk");
            println("DumpAdjustorThunks: " + currentProgram.getName() + " has no adjustor thunk in any vftable");
        }
        out.println("\n// thunk     this-=   target                                  vftable slot(s)");
        int attributed = 0;
        for (Thunk thunk : thunks) {
            if (!thunk.owners.isEmpty()) attributed++;
            out.printf("%s  %-7d %-40s %s%n", thunk.entry, thunk.displacement,
                    thunk.target.getName() + " @ " + thunk.target.getEntryPoint(),
                    thunk.owners.isEmpty() ? "-" : String.join(", ", thunk.owners));
        }
        out.println("\n// " + attributed + " of " + thunks.size()
                + " thunks are reached from a labelled vftable");
        out.close();
        println("DumpAdjustorThunks: " + thunks.size() + " thunks, " + renamed + " named, "
                + attributed + " attributed -> " + outPath);
    }

    /** `sub ecx,<imm>` (or `add ecx,-<imm>`) immediately followed by a jump to a function. */
    private Thunk match(Instruction instruction) {
        String mnemonic = instruction.getMnemonicString();
        boolean sub = mnemonic.equalsIgnoreCase("SUB");
        if (!sub && !mnemonic.equalsIgnoreCase("ADD")) return null;
        if (!"ECX".equals(instruction.getDefaultOperandRepresentation(0))) return null;
        if (instruction.getScalar(1) == null) return null;
        long value = instruction.getScalar(1).getSignedValue();
        long displacement = sub ? value : -value;
        if (displacement == 0) return null;

        // A thunk is a standalone stub, so the pair either stands outside every function or is
        // itself the entry of one; a match in the middle of a body is ordinary arithmetic.
        Function containing = getFunctionContaining(instruction.getAddress());
        if (containing != null && !containing.getEntryPoint().equals(instruction.getAddress())) return null;

        Instruction jump = instruction.getNext();
        if (jump == null || !jump.getFlowType().isJump() || !jump.getFlowType().isUnConditional()) return null;
        Address[] flows = jump.getFlows();
        if (flows.length != 1) return null;
        // The jump target is frequently not a function start yet -- it is only reached from
        // this thunk and from a vftable slot, so nothing has claimed it.
        Function target = getFunctionAt(flows[0]);
        if (target == null) target = getFunctionContaining(flows[0]);
        if (target == null) target = createFunction(flows[0], null);
        if (target == null) return null;
        return new Thunk(instruction.getAddress(), displacement, target);
    }

    /** True when some data slot points at this address, which for a stub means a vftable. */
    private boolean referencedFromData(Address thunk) {
        for (Reference reference : getReferencesTo(thunk)) {
            MemoryBlock block = getMemoryBlock(reference.getFromAddress());
            if (block != null && !block.isExecute()) return true;
        }
        return false;
    }

    /** The labelled vftables holding this thunk, as `Class[slot]`; empty without DumpRtti. */
    private List<String> owners(Address thunk) {
        List<String> owners = new ArrayList<>();
        for (Reference reference : getReferencesTo(thunk)) {
            Address from = reference.getFromAddress();
            MemoryBlock block = getMemoryBlock(from);
            if (block == null || block.isExecute()) continue;      // a call site, not a slot
            for (int slot = 0; slot < MAX_VFTABLE_SLOTS; slot++) {
                Address candidate;
                try { candidate = from.subtract(4L * slot); } catch (Exception e) { break; }
                Symbol symbol = getSymbolAt(candidate);
                if (symbol == null || !symbol.getName().startsWith("vftable_")) continue;
                owners.add(symbol.getName().substring("vftable_".length()) + "[" + slot + "]");
                break;
            }
        }
        return owners;
    }

    /** This script owns the `_adj<n>` suffix; drop its earlier output before writing again. */
    private void clearOwnNames() throws Exception {
        List<Function> stale = new ArrayList<>();
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            Symbol symbol = function.getSymbol();
            if (symbol != null && symbol.getSource() == SourceType.ANALYSIS
                    && symbol.getName().matches(".*_adj-?[0-9]+")) {
                stale.add(function);
            }
        }
        for (Function function : stale) function.setName(null, SourceType.DEFAULT);
    }

    private boolean isUnnamed(Function function) {
        if (function.getSymbol() == null) return true;
        return function.getSymbol().getSource() == SourceType.DEFAULT
                || function.getName().startsWith("thunk_FUN_");
    }

    private static final class Thunk {
        final Address entry;
        final long displacement;
        final Function target;
        List<String> owners = new ArrayList<>();

        Thunk(Address entry, long displacement, Function target) {
            this.entry = entry;
            this.displacement = displacement;
            this.target = target;
        }
    }
}
