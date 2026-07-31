import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.Reference;

import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

// Promotes disassembled-but-unowned code into functions.
//
// A plugin DLL whose subsystems are reached only through interface vtables
// (the stdshader_* shader classes) ends up with most of .text disassembled but
// almost none of it inside a Function. Every tool that walks the FunctionManager
// -- DumpGrep's `getFunctionContaining`, DumpFuncs' `range=` -- then reports the
// code as absent, and a string's referencing site resolves to `<none>`.
//
// Two seeds, both conservative: the target of every CALL, and the first
// instruction after a run of int3/nop padding. Nothing is disassembled that was
// not already; only ownership is added.
//
// Args (key=value, space separated):
//   block=<name>   memory block to sweep (default .text)
//   pad=<int>      padding bytes needed before a post-padding seed (default 3)
public class MakeFuncs extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String blockName = a.getOrDefault("block", ".text");
        int padNeeded = Integer.parseInt(a.getOrDefault("pad", "3"));

        MemoryBlock block = getMemoryBlock(blockName);
        if (block == null) { println("MakeFuncs: no block " + blockName); return; }
        AddressSetView body = currentProgram.getMemory().intersectRange(block.getStart(), block.getEnd());

        Set<Address> seeds = new LinkedHashSet<>();
        int padRun = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(body, true);
        Address prevEnd = null;
        while (it.hasNext()) {
            Instruction ins = it.next();

            // a gap since the previous instruction means padding (or data) sat
            // between them: the instruction after it starts something.
            if (prevEnd != null && ins.getAddress().subtract(prevEnd) >= padNeeded) seeds.add(ins.getAddress());
            prevEnd = ins.getMaxAddress().add(1);

            String m = ins.getMnemonicString();
            if (m.equals("INT3") || (m.equals("NOP"))) { padRun += ins.getLength(); continue; }
            if (padRun >= padNeeded) seeds.add(ins.getAddress());
            padRun = 0;

            for (Reference r : ins.getReferencesFrom()) {
                FlowType ft = ins.getFlowType();
                if (!ft.isCall()) continue;
                Address t = r.getToAddress();
                if (t != null && block.contains(t)) seeds.add(t);
            }
        }

        int made = 0, had = 0;
        for (Address s : seeds) {
            Function f = getFunctionAt(s);
            if (f != null) { had++; continue; }
            if (getInstructionAt(s) == null) continue;
            try { if (createFunction(s, null) != null) made++; } catch (Exception e) { /* overlapping body */ }
        }
        println("MakeFuncs: " + seeds.size() + " seeds, " + had + " already functions, " + made + " created");
    }
}
