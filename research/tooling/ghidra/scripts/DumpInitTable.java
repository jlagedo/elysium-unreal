import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

// Censuses a module's global constructors: every function MSVC runs before main.
//
// VtMB has no static DEFINE_FIELD arrays -- each class's datamap, each ConVar block and
// each static singleton is built by a per-class function that the CRT calls at startup.
// Finding one at a time means grepping a string and hoping the builder is the referencing
// function. The whole set is enumerable instead: __cinit passes the bounds of the
// initializer pointer tables to __initterm, and those tables list every one of them.
//
// The chain has two hops. A table slot points at an incremental-link thunk; the thunk
// reaches a small per-variable initializer the compiler emitted; that initializer calls
// the real builder, again through a thunk. So the census scores each initializer together
// with its direct callees and reports whichever member of that closure touches the most
// data -- which is the builder. CBaseCombatCharacter is the worked example: slot -> thunk
// -> staticinit_1031a5e0 -> thunk_FUN_1031a600 -> FUN_1031a600, the builder the roadmap's
// datamap work already knew by address.
//
// Both names come from the Function ID pass, so run this on a program the CRT database
// has been applied to (`uv run elysium research crt_fid apply <program>`).
//
// Default-named initializers are renamed staticinit_<hex>, which persists in the project
// and shows up in every later dump; an initializer that already carries a real name is
// left alone. Pass name=0 to census without writing.
//
// Args (key=value, space separated):
//   out=<path>      output file
//   cinit=<hex>     entry point of __cinit, if the FID pass did not name it
//   initterm=<hex>  entry point of __initterm, likewise
//   name=0          report only; do not rename default-named initializers
//   top=<int>       how many heaviest data touchers to list (default 40)
public class DumpInitTable extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/inittable.txt");
        boolean rename = !"0".equals(a.get("name"));
        int top = Integer.parseInt(a.getOrDefault("top", "40"));

        Function cinit = resolve(a.get("cinit"), "__cinit", "_cinit");
        Function initterm = resolve(a.get("initterm"), "__initterm", "_initterm");
        if (cinit == null || initterm == null) {
            printerr("DumpInitTable: __cinit and __initterm must be named or given as cinit=/initterm=;"
                    + " apply the CRT FID database first");
            return;
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName() + "  __cinit @ " + cinit.getEntryPoint()
                + "  __initterm @ " + initterm.getEntryPoint());

        List<Entry> census = new ArrayList<>();
        int renamed = 0;
        for (long[] bounds : initTermBounds(cinit, initterm)) {
            Address lo = toAddr(bounds[0]), hi = toAddr(bounds[1]);
            int slots = (int) ((bounds[1] - bounds[0]) / 4);
            int nonNull = 0;
            List<Entry> table = new ArrayList<>();
            for (int i = 0; i < slots; i++) {
                monitor.checkCancelled();
                long ptr;
                try { ptr = getInt(lo.add((long) i * 4)) & 0xffffffffL; }
                catch (Exception e) { break; }
                if (ptr == 0) continue;
                nonNull++;
                Address target = toAddr(ptr);
                MemoryBlock block = getMemoryBlock(target);
                if (block == null || !block.isExecute()) {
                    out.println("// slot " + i + " -> " + target + " is not code");
                    continue;
                }
                Function f = getFunctionAt(target);
                if (f == null) f = createFunction(target, null);
                if (f == null) {
                    out.println("// slot " + i + " -> " + target + " could not be made a function");
                    continue;
                }
                // MSVC's incremental linker puts a 5-byte JMP between the table and the
                // real initializer, so the slot's own function is a thunk with no body.
                Function body = resolveThunk(f);
                if (body != f) clearInitName(f);
                if (rename && body.getSymbol() != null
                        && body.getSymbol().getSource() == SourceType.DEFAULT) {
                    body.setName("staticinit_" + body.getEntryPoint(), SourceType.ANALYSIS);
                    renamed++;
                }
                table.add(score(body));
            }
            out.println("\n//======== __initterm(" + lo + ", " + hi + ")  "
                    + slots + " slots, " + nonNull + " non-null ========");
            for (Entry entry : table) {
                out.printf("%s  data=%-4d  size=%-6d  %s%n", entry.function.getEntryPoint(),
                        entry.dataRefs, entry.function.getBody().getNumAddresses(),
                        entry.label());
            }
            census.addAll(table);
        }

        // A per-class datamap builder touches the most distinct data addresses -- it fills
        // a typedescription_t array one field at a time. Sorting by that count puts the
        // builders at the top without knowing any class name.
        census.sort((x, y) -> Integer.compare(y.dataRefs, x.dataRefs));
        out.println("\n//======== heaviest data touchers (datamap and registration builders) ========");
        for (int i = 0; i < Math.min(top, census.size()); i++) {
            Entry entry = census.get(i);
            out.printf("%s  data=%-4d  %s%n", entry.builder.getEntryPoint(), entry.dataRefs,
                    entry.label());
        }

        out.println("\n// " + census.size() + " initializers, " + renamed + " renamed");
        out.close();
        println("DumpInitTable: " + census.size() + " initializers (" + renamed + " renamed) -> " + outPath);
    }

    /** Bounds of every pointer table __cinit hands to __initterm, as (lo, hi) pairs. */
    private List<long[]> initTermBounds(Function cinit, Function initterm) {
        List<long[]> bounds = new ArrayList<>();
        List<Long> pushed = new ArrayList<>();
        for (Instruction instruction : currentProgram.getListing().getInstructions(cinit.getBody(), true)) {
            String mnemonic = instruction.getMnemonicString();
            if (mnemonic.equalsIgnoreCase("PUSH")) {
                Object[] operands = instruction.getOpObjects(0);
                if (operands.length == 1) {
                    // The scalar operand analyzer may already have promoted the immediate
                    // to an Address, in which case it is no longer reported as a Scalar.
                    if (operands[0] instanceof Scalar) {
                        pushed.add(((Scalar) operands[0]).getUnsignedValue());
                    } else if (operands[0] instanceof Address) {
                        pushed.add(((Address) operands[0]).getOffset());
                    }
                }
                continue;
            }
            if (mnemonic.equalsIgnoreCase("CALL") && pushed.size() >= 2) {
                for (Reference reference : instruction.getReferencesFrom()) {
                    if (!initterm.getEntryPoint().equals(reference.getToAddress())) continue;
                    // cdecl pushes right to left, so the last two pushes are (hi, lo).
                    long first = pushed.get(pushed.size() - 1);
                    long second = pushed.get(pushed.size() - 2);
                    bounds.add(new long[] { Math.min(first, second), Math.max(first, second) });
                    break;
                }
            }
            pushed.clear();
        }
        return bounds;
    }

    /** How many distinct data addresses a function touches.

        Ghidra types an absolute memory operand as a plain DATA reference rather than a
        WRITE on x86, so direction is not a usable filter; the count of distinct targets
        is what separates a builder filling a record array from an ordinary constructor. */
    private int dataTargets(Function f) {
        TreeSet<Address> touched = new TreeSet<>();
        for (Instruction instruction : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            for (Reference reference : instruction.getReferencesFrom()) {
                Address target = reference.getToAddress();
                MemoryBlock block = getMemoryBlock(target);
                if (block == null || block.isExecute() || !block.isInitialized()) continue;
                touched.add(target);
            }
        }
        return touched.size();
    }

    /** Score an initializer by the heaviest data toucher among it and its direct callees. */
    private Entry score(Function initializer) throws Exception {
        Function builder = initializer;
        int best = dataTargets(initializer);
        for (Function callee : initializer.getCalledFunctions(monitor)) {
            Function body = resolveThunk(callee);
            int refs = dataTargets(body);
            if (refs > best) { best = refs; builder = body; }
        }
        return new Entry(initializer, builder, best);
    }

    /** The real initializer behind a table slot, following an incremental-link thunk. */
    private Function resolveThunk(Function f) {
        Function thunked = f.getThunkedFunction(true);
        if (thunked != null) return thunked;
        Instruction first = getInstructionAt(f.getEntryPoint());
        if (first != null && first.getFlowType().isJump() && first.getFlowType().isUnConditional()) {
            for (Address flow : first.getFlows()) {
                Function target = getFunctionAt(flow);
                if (target != null && !target.equals(f)) return target;
            }
        }
        return f;
    }

    /** Drop a staticinit_ name this script previously put on what turned out to be a thunk. */
    private void clearInitName(Function f) throws Exception {
        Symbol symbol = f.getSymbol();
        if (symbol != null && symbol.getSource() == SourceType.ANALYSIS
                && symbol.getName().startsWith("staticinit_")) {
            f.setName(null, SourceType.DEFAULT);
        }
    }

    private Function resolve(String override, String... names) {
        if (override != null && !override.isEmpty()) {
            Address address = toAddr(override);
            Function f = getFunctionAt(address);
            return f != null ? f : getFunctionContaining(address);
        }
        for (String name : names) {
            SymbolIterator symbols = currentProgram.getSymbolTable().getSymbols(name);
            while (symbols.hasNext()) {
                Symbol symbol = symbols.next();
                Function f = getFunctionAt(symbol.getAddress());
                if (f != null) return f;
            }
        }
        return null;
    }

    private static final class Entry {
        final Function function;
        final Function builder;
        final int dataRefs;

        Entry(Function function, Function builder, int dataRefs) {
            this.function = function;
            this.builder = builder;
            this.dataRefs = dataRefs;
        }

        String label() {
            return builder.equals(function) ? function.getName()
                    : function.getName() + " -> " + builder.getName();
        }
    }
}
