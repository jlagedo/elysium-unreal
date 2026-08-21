import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.Undefined4DataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// Recovers each function's parameter count from the stack discipline the image already states,
// and commits it -- so the decompiler stops inventing arguments.
//
// When a prototype is unknown the decompiler infers one from what the body reads, and a function
// whose frame it cannot follow acquires arguments that do not exist:
// `CBaseCombatCharacter::MeleeSwingUpdate` decompiles with 93 of them, plus a drift of
// `in_stack_*` and `unaff_*` locals. 1,814 functions are in that state. None of it is a mystery
// in the bytes -- x86 stack cleanup is part of the calling convention, so the argument size is
// written down twice.
//
// Two sources, and the exact one is tried first:
//
//   CALLEE-CLEANED (__thiscall, __stdcall -- almost all of VtMB's own code)
//       The function's own terminating `RET <imm16>` IS the argument byte count. Not an
//       inference: the instruction cannot be right about the stack and wrong about the count.
//
//   CALLER-CLEANED (__cdecl)
//       `RET` carries no immediate, so the count comes from the caller's `ADD ESP,<n>` after the
//       call. Read across every direct call site and taken modally, because one site may push a
//       shared frame or fold the cleanup into a later one.
//
// A function that ends `RET 0` and has no call site with a cleanup states nothing about its
// arguments, and is LEFT ALONE and counted -- an unknown prototype is better than a fabricated
// one, and the whole point of the pass is to stop fabrication.
//
// Args (key=value, space separated):
//   out=<path>   report file
//   apply=1      commit the signatures; the default reports only
//   max=<int>    reject a derived count above this many parameters (default 24)
public class RecoverSignatures extends GhidraScript {

    private static final int LOOKAHEAD = 4;

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
            printerr("RecoverSignatures: out=<path> is required");
            return;
        }
        boolean apply = "1".equals(a.get("apply"));
        int max = Integer.parseInt(a.getOrDefault("max", "24"));

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName()
                + (apply ? "  [applying]" : "  [report only]"));

        Listing listing = currentProgram.getListing();
        List<Function> functions = new ArrayList<>();
        FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
        while (all.hasNext()) functions.add(all.next());

        int fromRet = 0, fromCallers = 0, silent = 0, applied = 0, unchanged = 0;
        int rejected = 0, failed = 0;
        Map<Integer, Integer> histogram = new TreeMap<>();

        for (Function function : functions) {
            if (monitor.isCancelled()) break;
            if (function.isThunk() || function.isExternal()) continue;

            int bytes = -1;
            String source;
            int stated = calleeCleanup(listing, function);
            if (stated > 0) {
                bytes = stated;
                source = "RET";
                fromRet++;
            } else {
                int cleaned = callerCleanup(listing, function);
                if (cleaned > 0) {
                    bytes = cleaned;
                    source = "ADD ESP";
                    fromCallers++;
                } else {
                    silent++;
                    continue;
                }
            }
            if (bytes % 4 != 0) {
                out.println(function.getEntryPoint() + "  " + function.getName()
                        + "  " + source + " states " + bytes
                        + " bytes, not a multiple of 4 - left alone");
                rejected++;
                continue;
            }
            int count = bytes / 4;
            if (count > max) {
                out.println(function.getEntryPoint() + "  " + function.getName()
                        + "  " + source + " states " + count + " parameters, above max=" + max
                        + " - left alone");
                rejected++;
                continue;
            }
            histogram.merge(count, 1, Integer::sum);

            // The auto `this` of a __thiscall is not on the stack, so it is not part of the
            // count the cleanup states; Ghidra prepends it from the convention.
            int formal = countFormalParameters(function);
            if (formal == count) { unchanged++; continue; }
            out.printf("%s  %-50s %-8s %d -> %d parameters%n", function.getEntryPoint(),
                    function.getName(true), source, formal, count);
            if (!apply) continue;

            List<Variable> parameters = new ArrayList<>();
            for (int i = 1; i <= count; i++) {
                parameters.add(new ParameterImpl("param_" + i, Undefined4DataType.dataType,
                        currentProgram));
            }
            try {
                function.updateFunction(function.getCallingConventionName(), null, parameters,
                        FunctionUpdateType.DYNAMIC_STORAGE_FORMAL_PARAMS, true,
                        SourceType.ANALYSIS);
                applied++;
            } catch (Exception e) {
                out.println("  " + function.getEntryPoint() + " signature not committed: "
                        + e.getMessage());
                failed++;
            }
        }

        out.println("\n//======== parameter-count histogram ========");
        for (Map.Entry<Integer, Integer> entry : histogram.entrySet()) {
            out.printf("%3d params  %6d functions%n", entry.getKey(), entry.getValue());
        }
        String summary = functions.size() + " functions; " + fromRet + " state their argument"
                + " bytes in RET, " + fromCallers + " in a caller's ADD ESP, " + silent
                + " state nothing and were left alone; " + applied + " signatures committed, "
                + unchanged + " already correct, " + rejected + " rejected as implausible, "
                + failed + " failed to commit";
        out.println("\n// " + summary);
        out.close();
        println("RecoverSignatures: " + summary);
        println("RecoverSignatures: " + outPath);
    }

    /** How many bytes of arguments the function itself pops, from its own `RET <imm16>`.

        Every terminating RET of a function agrees on this by construction -- they are the same
        convention -- so the first one that carries an immediate answers it. */
    private int calleeCleanup(Listing listing, Function function) {
        for (Instruction instruction : listing.getInstructions(function.getBody(), true)) {
            if (!instruction.getMnemonicString().startsWith("RET")) continue;
            if (instruction.getNumOperands() == 0) continue;
            Object[] operands = instruction.getOpObjects(0);
            if (operands.length == 1 && operands[0] instanceof Scalar) {
                int value = (int) ((Scalar) operands[0]).getUnsignedValue();
                if (value > 0) return value;
            }
        }
        return 0;
    }

    /** The modal `ADD ESP,<n>` a caller executes after calling this function.

        Not always the instruction immediately after the CALL -- MSVC schedules a register move
        or a compare into the gap -- so a short window is searched, stopping at the next CALL so
        one function's cleanup is never read as another's. */
    private int callerCleanup(Listing listing, Function function) {
        Map<Integer, Integer> votes = new HashMap<>();
        int total = 0;
        for (Reference reference : getReferencesTo(function.getEntryPoint())) {
            if (!reference.getReferenceType().isCall()) continue;
            Instruction call = listing.getInstructionAt(reference.getFromAddress());
            if (call == null) continue;
            Instruction cursor = call.getNext();
            for (int step = 0; cursor != null && step < LOOKAHEAD; step++) {
                String mnemonic = cursor.getMnemonicString();
                if (mnemonic.equals("CALL")) break;
                if (mnemonic.equals("ADD") && cursor.getNumOperands() == 2
                        && "ESP".equals(cursor.getDefaultOperandRepresentation(0))) {
                    Object[] operands = cursor.getOpObjects(1);
                    if (operands.length == 1 && operands[0] instanceof Scalar) {
                        int value = (int) ((Scalar) operands[0]).getSignedValue();
                        if (value > 0 && value <= 256) {
                            votes.merge(value, 1, Integer::sum);
                            total++;
                        }
                    }
                    break;
                }
                cursor = cursor.getNext();
            }
        }
        if (total == 0) return 0;
        int best = 0, bestVotes = 0;
        for (Map.Entry<Integer, Integer> vote : votes.entrySet()) {
            if (vote.getValue() > bestVotes) { best = vote.getKey(); bestVotes = vote.getValue(); }
        }
        // A single dissenting site is normal; a split vote means the sites disagree about what
        // this function takes, and guessing between them would be the fabrication this pass
        // exists to remove.
        return bestVotes * 2 > total ? best : 0;
    }

    /** Formal parameters only: the auto `this` a __thiscall carries is not one. */
    private int countFormalParameters(Function function) {
        int count = 0;
        for (Parameter parameter : function.getParameters()) {
            if (!parameter.isAutoParameter()) count++;
        }
        return count;
    }
}
