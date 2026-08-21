import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// Reads a switch's jump table out of the image when the decompiler gave up on it.
//
// The decompiler abandons a table it cannot follow symbolically and says so:
// `Could not recover jumptable at <addr>. Too many branches` / `Treating indirect jump as call`.
// 1,283 functions are in that state, and the C it produced for them has the WRONG CONTROL FLOW --
// a switch reads as a call, cases vanish, and a reader who trusts the C concludes a case is not
// handled when it is. The already-documented example is `DispatchStartEvent`, which silently
// merged three enum cases and dropped a fourth.
//
// Nothing about the table is actually unknown. MSVC emits it as a stride-4 array of code
// addresses in .text, and the indexed `JMP` names its base as an immediate. This pass reads it.
//
// Two bounds keep a walk honest, and neither is a guess:
//   - an entry must land in executable memory, and
//   - the table cannot extend past the LOWEST address it points at, because MSVC lays the cases
//     out after the table. That bound tightens as the walk proceeds and is what stops a table
//     from running into the code it dispatches to.
//
// A table that fails either bound on its first entry is reported and left alone. The decompiler's
// wrong answer is bad; a fabricated table would be worse, because it would look right.
//
// Args (key=value, space separated):
//   out=<path>   report file
//   apply=1      create the references; the default reports only
//   max=<int>    reject a table longer than this many entries (default 512)
public class RecoverJumpTables extends GhidraScript {

    private static final int LOOKBACK = 6;

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.get("out");
        if (outPath == null) {
            printerr("RecoverJumpTables: out=<path> is required");
            return;
        }
        boolean apply = "1".equals(a.get("apply"));
        int max = Integer.parseInt(a.getOrDefault("max", "512"));

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName()
                + (apply ? "  [applying]" : "  [report only]"));

        List<Instruction> indirect = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isExecute() || !block.isInitialized()) continue;
            InstructionIterator instructions = currentProgram.getListing()
                    .getInstructions(block.getStart(), true);
            while (instructions.hasNext()) {
                Instruction instruction = instructions.next();
                if (instruction.getAddress().compareTo(block.getEnd()) > 0) break;
                if (!instruction.getFlowType().isJump()) continue;
                if (!instruction.getFlowType().isComputed()) continue;
                indirect.add(instruction);
            }
        }

        int recovered = 0, alreadyKnown = 0, noBase = 0, empty = 0, references = 0;
        for (Instruction jump : indirect) {
            if (monitor.isCancelled()) break;
            Function owner = getFunctionContaining(jump.getAddress());
            String where = jump.getAddress() + "  "
                    + (owner == null ? "<no function>" : owner.getName(true));

            int known = 0;
            for (Reference reference : jump.getReferencesFrom()) {
                if (reference.getReferenceType().isJump()
                        && reference.getReferenceType().isComputed()) known++;
            }
            if (known > 1) { alreadyKnown++; continue; }

            Address base = tableBase(jump);
            if (base == null) {
                out.println(where + "  no table base in the instruction or its lookback"
                        + " - left alone");
                noBase++;
                continue;
            }
            List<Address> entries = readTable(base, max);
            if (entries.isEmpty()) {
                out.println(where + "  table at " + base + " has no valid first entry"
                        + " - left alone");
                empty++;
                continue;
            }
            Set<Address> distinct = new LinkedHashSet<>(entries);
            out.printf("%s  table %s  %d entries (%d distinct)%n", where, base, entries.size(),
                    distinct.size());
            recovered++;
            if (!apply) continue;

            try {
                createDwords(base, entries.size());
            } catch (Exception e) {
                out.println("    table not typed as dwords: " + e.getMessage());
            }
            for (Address target : distinct) {
                try {
                    if (getInstructionAt(target) == null) disassemble(target);
                    currentProgram.getReferenceManager().addMemoryReference(
                            jump.getAddress(), target, RefType.COMPUTED_JUMP,
                            SourceType.ANALYSIS, 0);
                    references++;
                } catch (Exception e) {
                    out.println("    " + target + " not referenced: " + e.getMessage());
                }
            }
        }

        String summary = indirect.size() + " computed jumps; " + alreadyKnown
                + " already have a recovered table, " + recovered + " tables read from the image ("
                + references + " references created), " + noBase + " state no base, " + empty
                + " have no valid entry";
        out.println("\n// " + summary);
        out.close();
        println("RecoverJumpTables: " + summary);
        println("RecoverJumpTables: " + outPath);
    }

    /** The table's base address, from the indexed operand of the jump or of a recent load.

        MSVC emits either `JMP dword ptr [reg*4 + <base>]` or a two-instruction form that loads
        through the same addressing mode into a register first, so a short lookback is searched
        when the jump itself carries no immediate. */
    private Address tableBase(Instruction jump) {
        Address base = scaledBase(jump);
        if (base != null) return base;
        Instruction cursor = jump.getPrevious();
        for (int step = 0; cursor != null && step < LOOKBACK; step++) {
            if (cursor.getFlowType().isCall() || cursor.getFlowType().isJump()) break;
            base = scaledBase(cursor);
            if (base != null) return base;
            cursor = cursor.getPrevious();
        }
        return null;
    }

    /** The displacement of a `[reg*4 + <disp>]` operand, when it addresses initialized memory. */
    private Address scaledBase(Instruction instruction) {
        for (int operand = 0; operand < instruction.getNumOperands(); operand++) {
            Object[] objects = instruction.getOpObjects(operand);
            boolean scaledByFour = false;
            long displacement = 0;
            for (Object object : objects) {
                if (!(object instanceof Scalar)) continue;
                long value = ((Scalar) object).getUnsignedValue();
                if (value == 4) scaledByFour = true;
                else if (value > displacement) displacement = value;
            }
            if (!scaledByFour || displacement == 0) continue;
            Address candidate;
            try { candidate = toAddr(displacement); } catch (Exception e) { continue; }
            MemoryBlock block = getMemoryBlock(candidate);
            if (block != null && block.isInitialized()) return candidate;
        }
        return null;
    }

    /** Stride-4 code addresses, bounded by executable memory and by the table's own targets. */
    private List<Address> readTable(Address base, int max) {
        List<Address> entries = new ArrayList<>();
        // MSVC lays the cases out after the table, so the lowest target seen so far is a hard
        // ceiling on how far the table can run. It tightens with every entry read.
        long ceiling = Long.MAX_VALUE;
        for (int i = 0; i < max; i++) {
            long at = base.getOffset() + i * 4L;
            if (at >= ceiling) break;
            long value;
            try { value = getInt(toAddr(at)) & 0xFFFFFFFFL; } catch (Exception e) { break; }
            Address target;
            try { target = toAddr(value); } catch (Exception e) { break; }
            MemoryBlock block = getMemoryBlock(target);
            if (block == null || !block.isExecute()) break;
            entries.add(target);
            if (value > base.getOffset() && value < ceiling) ceiling = value;
        }
        return entries;
    }
}
