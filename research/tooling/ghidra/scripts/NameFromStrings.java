import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Names a module's functions from the symbol-shaped strings compiled into it. A Source build
// leaves its own identifiers in the image: a VProf scope pushed at a function's entry is that
// function's name (`C_BaseAnimating::SetupBones`, `CGameMovement::PlayerMove`), an input handler
// opens by pushing `C<Class>::Input<Name>`, and an assert carries the source file it was written
// in. This pass reads those, attributes each to the function that references it, and renames.
//
// Two tiers, and only one of them renames. A qualified name referenced within `entry=` bytes of
// a function's entry point is the strong tier — that is the profiler-scope push, and it names
// the function it opens. Anything referenced further in is reported and never written: it is as
// likely to name a callee, or to be a message about another class.
//
// Ambiguity is reported, not guessed: a function carrying two strong candidates, or a name that
// two functions both claim strongly, is skipped. Only a default-named function is renamed, so a
// name the Function ID pass already wrote (`crt_fid apply`) is never overwritten.
//
// Source paths never rename anything. They are collected into a file -> function map, which
// recovers the module's own source-tree grouping.
//
// Ownership is the plate comment. A renamed function carries a `NameFromStrings:` line naming
// the string and its address, which is both the evidence a later reader wants and the marker
// `clear=1` uses to undo the pass, so re-running is idempotent and corrects an earlier run.
//
// Args (key=value, space separated):
//   out=<path>    report file
//   name=1        rename; the default reports only (the survey phase)
//   entry=<int>   bytes from the entry point that count as the strong tier (default 128)
//   clear=1       revert this pass's own renames and exit
public class NameFromStrings extends GhidraScript {

    // A whole string that is nothing but a qualified member name: the VProf scope form.
    private static final Pattern EXACT =
            Pattern.compile("^([A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*)$");
    // A qualified member name opening a longer message: an assert or a warning about that call.
    private static final Pattern LEADING =
            Pattern.compile("^([A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*)\\b.*");
    private static final Pattern SOURCE_FILE =
            Pattern.compile("^.*[\\\\/][^\\\\/]+\\.(c|cc|cpp|cxx|h|hpp)$", Pattern.CASE_INSENSITIVE);

    private static final String TAG = "NameFromStrings:";

    private int entrySpan;

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/names.txt");
        entrySpan = Integer.parseInt(a.getOrDefault("entry", "128"));
        boolean rename = "1".equals(a.get("name"));

        if ("1".equals(a.get("clear"))) {
            println("NameFromStrings: reverted " + clearOwnNames() + " renames");
            return;
        }

        // Candidates per function, and the counter-index that decides ambiguity.
        Map<Address, List<Candidate>> strong = new LinkedHashMap<>();
        Map<Address, List<Candidate>> weak = new LinkedHashMap<>();
        Map<String, List<Address>> claimants = new TreeMap<>();
        Map<String, List<String>> sourceFiles = new TreeMap<>();
        int symbolStrings = 0, unreferenced = 0, unownedSites = 0, fileStrings = 0;

        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext() && !monitor.isCancelled()) {
            Data item = data.next();
            String text = stringOf(item);
            if (text == null || text.length() < 5 || text.length() > 200) continue;

            if (SOURCE_FILE.matcher(text).matches()) {
                fileStrings++;
                for (Reference reference : getReferencesTo(item.getAddress())) {
                    Function owner = owningFunction(reference);
                    if (owner == null) continue;
                    sourceFiles.computeIfAbsent(text, k -> new ArrayList<>())
                            .add(owner.getName() + "  " + owner.getEntryPoint());
                }
                continue;
            }

            Matcher exact = EXACT.matcher(text);
            Matcher leading = LEADING.matcher(text);
            boolean isExact = exact.matches();
            if (!isExact && !leading.matches()) continue;
            String className = isExact ? exact.group(1) : leading.group(1);
            String member = isExact ? exact.group(2) : leading.group(2);
            symbolStrings++;

            boolean referenced = false;
            for (Reference reference : getReferencesTo(item.getAddress())) {
                MemoryBlock block = getMemoryBlock(reference.getFromAddress());
                if (block == null || !block.isExecute()) continue;
                referenced = true;
                Function owner = getFunctionContaining(reference.getFromAddress());
                if (owner == null) { unownedSites++; continue; }

                long distance = reference.getFromAddress().getOffset()
                        - owner.getEntryPoint().getOffset();
                Candidate candidate = new Candidate(className, member, text,
                        item.getAddress(), distance);
                if (isExact && distance >= 0 && distance <= entrySpan) {
                    strong.computeIfAbsent(owner.getEntryPoint(), k -> new ArrayList<>())
                            .add(candidate);
                    claimants.computeIfAbsent(candidate.qualified(), k -> new ArrayList<>())
                            .add(owner.getEntryPoint());
                } else {
                    weak.computeIfAbsent(owner.getEntryPoint(), k -> new ArrayList<>())
                            .add(candidate);
                }
            }
            if (!referenced) unreferenced++;
        }

        if (rename) clearOwnNames();

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName()
                + "  strong tier <= " + entrySpan + " bytes from entry"
                + (rename ? "  [renaming]" : "  [survey only]"));

        int renamed = 0, skippedNamed = 0, ambiguous = 0;
        // A name claimed by many functions at once is an inlined helper, not a collision: its
        // scope marker rides into every caller that inlined it. One line per name, not per
        // claimant, and the claim count is the finding.
        Map<String, String> conflicts = new LinkedHashMap<>();
        out.println("\n//======== strong: a qualified name referenced at the function's own entry ========");
        List<Address> entries = new ArrayList<>(strong.keySet());
        entries.sort(Comparator.comparingLong(Address::getOffset));
        for (Address entry : entries) {
            List<Candidate> candidates = distinct(strong.get(entry));
            Function function = getFunctionAt(entry);
            if (function == null) continue;
            if (candidates.size() > 1) {
                ambiguous++;
                conflicts.put(entry.toString(),
                        entry + "  " + function.getName() + "  claims " + names(candidates));
                continue;
            }
            Candidate candidate = candidates.get(0);
            List<Address> others = distinctAddresses(claimants.get(candidate.qualified()));
            if (others.size() > 1) {
                ambiguous++;
                conflicts.put(candidate.qualified(), candidate.qualified() + "  claimed by "
                        + others.size() + (others.size() > 8
                            ? " (inlined) " + others.subList(0, 8) + " …"
                            : " " + others));
                continue;
            }
            boolean isDefault = function.getSymbol() == null
                    || function.getSymbol().getSource() == SourceType.DEFAULT;
            out.printf("%-12s %-46s %s%n", entry, candidate.qualified(),
                    isDefault ? function.getName() : function.getName() + "  [already named - kept]");
            if (!isDefault) { skippedNamed++; continue; }
            if (rename) { applyName(function, candidate); renamed++; }
        }

        out.println("\n//======== ambiguous: reported, never written ("
                + conflicts.size() + " distinct) ========");
        for (String line : conflicts.values()) out.println(line);

        out.println("\n//======== weak: a qualified name referenced further into a function ========");
        List<Address> weakEntries = new ArrayList<>(weak.keySet());
        weakEntries.sort(Comparator.comparingLong(Address::getOffset));
        for (Address entry : weakEntries) {
            Function function = getFunctionAt(entry);
            if (function == null) continue;
            for (Candidate candidate : distinct(weak.get(entry))) {
                out.printf("%-12s %-46s %s  +0x%x%n", entry, candidate.qualified(),
                        function.getName(), candidate.distance);
            }
        }

        out.println("\n//======== source files: the module's own tree, from assert and warning paths ========");
        for (Map.Entry<String, List<String>> file : sourceFiles.entrySet()) {
            out.println(file.getKey() + "  (" + file.getValue().size() + ")");
            for (String site : file.getValue()) out.println("    " + site);
        }

        int functions = 0;
        FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
        while (all.hasNext()) { all.next(); functions++; }

        String summary = symbolStrings + " qualified-name strings, " + fileStrings + " source paths, "
                + strong.size() + " strong-tier functions, " + ambiguous + " ambiguous, "
                + skippedNamed + " already named, " + weak.size() + " weak-tier functions, "
                + unreferenced + " unreferenced strings, " + unownedSites
                + " sites in unowned code, " + functions + " functions in the program"
                + (rename ? ", " + renamed + " renamed" : "");
        out.println("\n// " + summary);
        // Unowned sites are the MakeFuncs signal: attribution is getFunctionContaining, so
        // disassembled-but-unowned code silently contributes nothing.
        if (unownedSites > 0) {
            out.println("// " + unownedSites + " referencing sites lie in code no Function owns -"
                    + " run MakeFuncs on this program and re-survey.");
        }
        out.close();
        println("NameFromStrings: " + summary + " -> " + outPath);
        if (unownedSites > 0) {
            printerr("NameFromStrings: " + unownedSites + " referencing sites lie in unowned code;"
                    + " run MakeFuncs on " + currentProgram.getName() + " and re-survey.");
        }
    }

    /** Rename into the class namespace, and record the evidence that owns the rename. */
    private void applyName(Function function, Candidate candidate) throws Exception {
        SymbolTable symbols = currentProgram.getSymbolTable();
        Namespace global = currentProgram.getGlobalNamespace();
        Namespace parent = null;
        try {
            parent = symbols.getNamespace(candidate.className, global);
            if (parent == null) {
                parent = symbols.createNameSpace(global, candidate.className, SourceType.ANALYSIS);
            }
        } catch (Exception e) {
            // A global symbol already owns the class name, so the namespace cannot be created.
            // The name is still recoverable flat; losing it because of a container is worse.
            printerr("NameFromStrings: no namespace for " + candidate.className + " ("
                    + e.getMessage() + "); naming flat");
        }
        if (parent != null) function.setParentNamespace(parent);
        function.setName(parent != null ? candidate.member
                : candidate.className + "_" + candidate.member, SourceType.ANALYSIS);
        String note = TAG + " \"" + candidate.text + "\" @ " + candidate.stringAddress;
        String existing = function.getComment();
        function.setComment(existing == null || existing.isEmpty() ? note : note + "\n" + existing);
    }

    /** This pass owns every function carrying its plate-comment tag; drop what an earlier run wrote. */
    private int clearOwnNames() throws Exception {
        int reverted = 0;
        Namespace global = currentProgram.getGlobalNamespace();
        List<Function> owned = new ArrayList<>();
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext()) {
            Function function = functions.next();
            String comment = function.getComment();
            if (comment != null && comment.contains(TAG)) owned.add(function);
        }
        for (Function function : owned) {
            List<String> kept = new ArrayList<>();
            for (String line : function.getComment().split("\n")) {
                if (!line.startsWith(TAG)) kept.add(line);
            }
            function.setName(null, SourceType.DEFAULT);
            function.setParentNamespace(global);
            function.setComment(kept.isEmpty() ? null : String.join("\n", kept));
            reverted++;
        }
        return reverted;
    }

    private Function owningFunction(Reference reference) {
        MemoryBlock block = getMemoryBlock(reference.getFromAddress());
        if (block == null || !block.isExecute()) return null;
        return getFunctionContaining(reference.getFromAddress());
    }

    private String stringOf(Data item) {
        if (item == null || item.getDataType() == null) return null;
        StringDataInstance sdi = StringDataInstance.getStringDataInstance(item);
        if (sdi == null || sdi.getStringLength() <= 0) return null;
        return sdi.getStringValue();
    }

    private static List<Candidate> distinct(List<Candidate> candidates) {
        Map<String, Candidate> byName = new LinkedHashMap<>();
        for (Candidate candidate : candidates) {
            Candidate seen = byName.get(candidate.qualified());
            if (seen == null || candidate.distance < seen.distance) {
                byName.put(candidate.qualified(), candidate);
            }
        }
        return new ArrayList<>(byName.values());
    }

    private static List<Address> distinctAddresses(List<Address> addresses) {
        List<Address> unique = new ArrayList<>();
        if (addresses == null) return unique;
        for (Address address : addresses) if (!unique.contains(address)) unique.add(address);
        return unique;
    }

    private static String names(List<Candidate> candidates) {
        List<String> qualified = new ArrayList<>();
        for (Candidate candidate : candidates) qualified.add(candidate.qualified());
        return String.join(", ", qualified);
    }

    private static final class Candidate {
        final String className, member, text;
        final Address stringAddress;
        final long distance;

        Candidate(String className, String member, String text, Address stringAddress, long distance) {
            this.className = className;
            this.member = member;
            this.text = text;
            this.stringAddress = stringAddress;
            this.distance = distance;
        }

        String qualified() { return className + "::" + member; }
    }
}
