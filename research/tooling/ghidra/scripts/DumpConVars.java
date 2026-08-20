import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// Recovers the ConVar / ConCommand registration table of a Source DLL: for every
// call to a registration constructor, walks back over the call site's PUSHes to recover
// the name string and default value, and the MOV ECX,<imm> that names the ConVar
// object itself. That object address is what every reader in the decompilation
// shows (`DAT_104d205c`), so this is the lookup that turns an anonymous global in
// a decompiled function back into a cvar name.
//
// With no `ctor=`, the constructors are found rather than supplied, and the anchor is exact
// rather than shape-based: a constructor is a function that stores the ConVar, ConCommand or
// ConCommandBase vftable into its object. **Run `DumpRtti` first** -- it is what labels those
// vftables, and without the labels there is nothing to anchor to. Guessing instead from the
// shape of a call site does not work here: plenty of registration tables take a static object
// and a string literal, and VtMB's AI schedule, condition and squad-slot registrations all
// look identical to a ConVar at the call site.
//
// Each recovered object is labelled `cvar_<name>`, so a later dump of any reader shows the
// console variable instead of an anonymous global. The script owns the `cvar_` label
// namespace and clears it first, so re-running is idempotent and corrects itself.
//
// Args (key=value, space separated):
//   ctor=<hex>       a constructor's entry point; omit to find every one
//   vftables=<hex+>  override the anchor vftables (default: the DumpRtti labels)
//   out=<path>       output file
//   back=<int>       how many instructions to walk back from a call site (default 14)
//   name=0           report only; label nothing
public class DumpConVars extends GhidraScript {

    private static final String[] ANCHOR_CLASSES = { "ConVar", "ConCommand", "ConCommandBase" };

    private int back;

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/convars.txt");
        back = Integer.parseInt(a.getOrDefault("back", "14"));
        boolean label = !"0".equals(a.get("name"));

        List<Address> ctors = new ArrayList<>();
        if (a.get("ctor") != null) {
            ctors.add(toAddr(a.get("ctor")));
        } else {
            List<Address> anchors = anchorVftables(a.get("vftables"));
            if (anchors.isEmpty()) {
                printerr("DumpConVars: no ConVar/ConCommand vftable found; run DumpRtti first,"
                        + " or pass vftables=<hex+>");
                return;
            }
            ctors = constructors(anchors);
            if (ctors.isEmpty()) {
                printerr("DumpConVars: vftables located but no function stores them; the"
                        + " constructors are not recognisable in this module");
                return;
            }
        }

        if (label) clearOwnLabels();

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName() + "  " + ctors.size()
                + " registration constructor(s)");

        int total = 0, labelled = 0;
        for (Address ctor : ctors) {
            Function function = getFunctionAt(ctor);
            List<Site> sites = sites(ctor);
            out.println("\n//======== " + ctor + "  "
                    + (function == null ? "" : function.getName() + "  ")
                    + sites.size() + " registrations ========");
            out.println("// object      name                            args (first = last PUSH before CALL)");
            for (Site site : sites) {
                out.printf("%-12s %-32s %s%n", site.object == null ? "?" : site.object.toString(),
                        site.name == null ? "?" : site.name, String.join("  ", site.args));
                total++;
                if (label && site.object != null && site.name != null) {
                    createLabel(site.object, "cvar_" + sanitize(site.name), true, SourceType.ANALYSIS);
                    labelled++;
                }
            }
        }
        out.println("\n// " + total + " registrations, " + labelled + " objects labelled");
        out.close();
        println("DumpConVars: " + ctors.size() + " constructors, " + total + " registrations, "
                + labelled + " labelled -> " + outPath);
    }

    /** The vftables of the console-command classes, from the labels DumpRtti wrote. */
    private List<Address> anchorVftables(String override) {
        List<Address> anchors = new ArrayList<>();
        if (override != null && !override.isEmpty()) {
            for (String value : override.split("[;,+]")) {
                if (!value.isEmpty()) anchors.add(toAddr(value));
            }
            return anchors;
        }
        for (String className : ANCHOR_CLASSES) {
            SymbolIterator symbols = currentProgram.getSymbolTable().getSymbols("vftable_" + className);
            while (symbols.hasNext()) anchors.add(symbols.next().getAddress());
        }
        return anchors;
    }

    /** Functions that store an anchor vftable into an object: the registration constructors. */
    private List<Address> constructors(List<Address> anchors) {
        Set<Address> found = new LinkedHashSet<>();
        for (Address anchor : anchors) {
            for (Reference reference : getReferencesTo(anchor)) {
                MemoryBlock block = getMemoryBlock(reference.getFromAddress());
                if (block == null || !block.isExecute()) continue;
                Instruction instruction = getInstructionAt(reference.getFromAddress());
                if (instruction == null || !instruction.getMnemonicString().equalsIgnoreCase("MOV")) continue;
                Function function = getFunctionContaining(reference.getFromAddress());
                if (function != null) found.add(function.getEntryPoint());
            }
        }
        return new ArrayList<>(found);
    }

    /** Every call site of a constructor, with the object it constructs and its arguments.

        Call sites reference the incremental-link thunk, not the constructor itself, so the
        thunks have to be swept too or every constructor reports zero registrations. */
    private List<Site> sites(Address ctor) {
        List<Address> entries = new ArrayList<>();
        entries.add(ctor);
        Function function = getFunctionAt(ctor);
        if (function != null) {
            Address[] thunks = function.getFunctionThunkAddresses(true);
            if (thunks != null) for (Address thunk : thunks) entries.add(thunk);
        }
        List<Site> sites = new ArrayList<>();
        for (Address entry : entries) sites.addAll(sitesOf(entry));
        return sites;
    }

    private List<Site> sitesOf(Address entry) {
        List<Site> sites = new ArrayList<>();
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(entry);
        while (it.hasNext()) {
            Reference r = it.next();
            Instruction call = getInstructionAt(r.getFromAddress());
            if (call == null || !call.getMnemonicString().startsWith("CALL")) continue;

            Site site = new Site();
            Instruction ins = call.getPrevious();
            for (int i = 0; i < back && ins != null; i++, ins = ins.getPrevious()) {
                String m = ins.getMnemonicString();
                if (m.equals("CALL")) break;                       // previous call site
                if (m.equals("MOV") && "ECX".equals(ins.getDefaultOperandRepresentation(0))) {
                    Scalar s = ins.getScalar(1);
                    if (s != null && site.object == null) {
                        try { site.object = toAddr(s.getUnsignedValue()); }
                        catch (Exception e) { /* the immediate is not an address */ }
                    }
                }
                else if (m.equals("PUSH")) {
                    Scalar s = ins.getScalar(0);
                    site.args.add(s == null ? ins.getDefaultOperandRepresentation(0)
                            : describe(s.getUnsignedValue()));
                }
            }
            // Arguments are pushed right to left, and this walks backwards from the call, so
            // args[0] is the leftmost parameter. Every registration form names itself first,
            // but ConCommand also passes a help string, so take the first literal, not any.
            for (String arg : site.args) {
                String text = unquote(arg);
                if (text != null) { site.name = text; break; }
            }
            sites.add(site);
        }
        return sites;
    }

    /** This script owns `cvar_`; drop its earlier output so a re-run cannot leave a stale name. */
    private void clearOwnLabels() throws Exception {
        List<Symbol> stale = new ArrayList<>();
        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbolIterator("cvar_*", true);
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            if (symbol.getSource() == SourceType.ANALYSIS) stale.add(symbol);
        }
        for (Symbol symbol : stale) removeSymbol(symbol.getAddress(), symbol.getName());
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
            // A default value is usually untyped data, so Ghidra reports no string there;
            // read the bytes instead rather than printing the pointer.
            String literal = readCString(addr);
            if (literal != null) return "\"" + literal + "\"";
        }
        return hex(v);
    }

    private String readCString(Address address) {
        MemoryBlock block = getMemoryBlock(address);
        if (block == null || !block.isInitialized() || block.isExecute()) return null;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 96; i++) {
            byte c;
            try { c = getByte(address.add(i)); } catch (Exception e) { return null; }
            if (c == 0) return sb.length() > 0 ? sb.toString() : null;
            if (c < 0x20 || c > 0x7e) return null;
            sb.append((char) c);
        }
        return null;
    }

    private static String unquote(String value) {
        if (value.length() < 3 || value.charAt(0) != '"') return null;
        return value.substring(1, value.length() - 1);
    }

    private static String sanitize(String name) {
        StringBuilder sb = new StringBuilder();
        for (char c : name.toCharArray()) {
            sb.append(Character.isLetterOrDigit(c) || c == '_' ? c : '_');
        }
        return sb.length() == 0 ? "unnamed" : sb.toString();
    }

    private String hex(long v) { return "0x" + Long.toHexString(v); }

    private static final class Site {
        Address object;
        String name;
        final List<String> args = new ArrayList<>();
    }
}
