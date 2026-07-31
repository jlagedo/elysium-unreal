import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class DumpMenu extends GhidraScript {

    static final String OUT = "E:/dev/elysium/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/menu_recon.txt";

    // class/namespace name keywords -> decompile every method of the class
    static final Pattern CLS = Pattern.compile("(?i)(gamemenu|basepanel|menuitem|mainmenu)");
    // string keywords -> any function referencing such a string is interesting
    static final Pattern STR = Pattern.compile("(?i)(gamemenu|OpenNewGameDialog|OpenLoadGameDialog|OpenSaveGameDialog|OpenOptionsDialog|OpenCreateMultiplayer|OpenServerBrowser|ResumeGame|Disconnect|QuitNoConfirm|resource/gamemenu)");
    // function name keywords
    static final Pattern FN = Pattern.compile("(?i)(gamemenu|basepanel|performlayout|createmenu|createbottom|layoutbasepanel|recalculatewidth|positiondialog)");

    public void run() throws Exception {
        PrintWriter out = new PrintWriter(new FileWriter(OUT));
        Program p = currentProgram;

        // ---- 1) RTTI-recovered classes ----
        out.println("======== CLASSES (RTTI namespaces) ========");
        List<String> classes = new ArrayList<>();
        Iterator<GhidraClass> cit = p.getSymbolTable().getClassNamespaces();
        while (cit.hasNext()) classes.add(cit.next().getName(true));
        Collections.sort(classes);
        for (String c : classes) out.println(c);
        out.println("total classes: " + classes.size());

        // ---- 2) menu-ish strings + referencing functions ----
        out.println("\n======== MENU-RELATED STRINGS ========");
        Set<Function> viaString = new LinkedHashSet<>();
        for (Data d : p.getListing().getDefinedData(true)) {
            Object v;
            try { v = d.getValue(); } catch (Exception ex) { continue; }
            if (!(v instanceof String)) continue;
            String s = (String) v;
            if (!STR.matcher(s).find()) continue;
            StringBuilder refs = new StringBuilder();
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) { viaString.add(f); refs.append(f.getName()).append(" "); }
            }
            out.println(d.getAddress() + "  \"" + s.replace("\n","\\n") + "\"  <- " + refs);
        }

        // ---- 3) collect interesting functions ----
        LinkedHashMap<Function,String> interesting = new LinkedHashMap<>();
        for (Function f : p.getFunctionManager().getFunctions(true)) {
            String full = f.getName(true);          // Namespace::name
            String reason = null;
            if (CLS.matcher(full).find()) reason = "class";
            else if (FN.matcher(f.getName()).find()) reason = "fname";
            if (reason != null) interesting.put(f, reason);
        }
        for (Function f : viaString) interesting.putIfAbsent(f, "string-ref");

        out.println("\n======== INTERESTING FUNCTIONS (" + interesting.size() + ") ========");
        for (Map.Entry<Function,String> e : interesting.entrySet())
            out.println(String.format("%-10s %-12s %s", e.getKey().getEntryPoint(), e.getValue(), e.getKey().getName(true)));

        // ---- 4) decompile them ----
        DecompInterface dec = new DecompInterface();
        dec.toggleCCode(true);
        dec.openProgram(p);
        int n = 0;
        for (Function f : interesting.keySet()) {
            if (n++ > 80) { out.println("\n[capped at 80 functions]"); break; }
            out.println("\n\n//======================================================");
            out.println("// " + f.getName(true) + "   @ " + f.getEntryPoint()
                        + "   reason=" + interesting.get(f));
            out.println("//======================================================");
            try {
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null)
                    out.println(r.getDecompiledFunction().getC());
                else
                    out.println("// <decompile failed: " + (r==null?"null":r.getErrorMessage()) + ">");
            } catch (Exception ex) {
                out.println("// <exception: " + ex + ">");
            }
        }
        dec.dispose();
        out.close();
        println("DumpMenu: wrote " + OUT + " (" + classes.size() + " classes, "
                + interesting.size() + " functions)");
    }
}
