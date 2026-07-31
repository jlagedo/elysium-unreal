import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

// Parameterized recon: dump RTTI classes, defined strings matching a regex (+ the
// functions that reference them), functions whose name matches a regex, and
// decompile the interesting set. Reusable for any subsystem — feed regexes on the
// command line so no recompile is needed to chase a new topic.
//
// Args (key=value; use '~' for alternation, NEVER spaces/commas/pipes — the arg
// string passes through run.ps1 (splits ' '), headless (splits '=' ','), and
// cmd.exe/analyzeHeadless.bat (a bare '|' becomes a shell pipe)):
//   str=<regex>   defined-string keywords (referencing funcs become interesting)
//   cls=<regex>   RTTI class/namespace name keywords (every method decompiled)
//   fn=<regex>    function-name keywords
//   out=<path>    output file
//   cap=<int>     max functions to decompile (default 60)
//   listcls=1     also list ALL class namespaces (recon of what RTTI recovered)
public class DumpGrep extends GhidraScript {

    public void run() throws Exception {
        Map<String,String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0,eq), raw[i].substring(eq+1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i+1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/dev/elysium/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/grep.txt");
        int cap = Integer.parseInt(a.getOrDefault("cap", "60"));
        Pattern STR = pat(a.get("str"));
        Pattern CLS = pat(a.get("cls"));
        Pattern FN  = pat(a.get("fn"));
        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Program p = currentProgram;
        out.println("// program: " + p.getName() + "  imagebase " + p.getImageBase());
        out.println("// str=" + a.get("str") + "  cls=" + a.get("cls") + "  fn=" + a.get("fn"));

        // ---- optional: list all RTTI classes ----
        if ("1".equals(a.get("listcls"))) {
            out.println("\n======== ALL CLASS NAMESPACES ========");
            List<String> classes = new ArrayList<>();
            Iterator<GhidraClass> cit = p.getSymbolTable().getClassNamespaces();
            while (cit.hasNext()) classes.add(cit.next().getName(true));
            Collections.sort(classes);
            for (String c : classes) out.println(c);
            out.println("total classes: " + classes.size());
        }

        // ---- strings matching STR + referencing functions ----
        LinkedHashMap<Function,String> interesting = new LinkedHashMap<>();
        if (STR != null) {
            out.println("\n======== MATCHING STRINGS (str) ========");
            for (Data d : p.getListing().getDefinedData(true)) {
                Object v;
                try { v = d.getValue(); } catch (Exception ex) { continue; }
                if (!(v instanceof String)) continue;
                String s = (String) v;
                if (!STR.matcher(s).find()) continue;
                StringBuilder refs = new StringBuilder();
                for (Reference r : getReferencesTo(d.getAddress())) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f != null) { interesting.putIfAbsent(f, "str-ref"); refs.append(f.getName()).append(" "); }
                }
                out.println(d.getAddress() + "  \"" + s.replace("\n","\\n") + "\"  <- " + refs);
            }
        }

        // ---- functions by class / name ----
        for (Function f : p.getFunctionManager().getFunctions(true)) {
            String full = f.getName(true);
            if (CLS != null && CLS.matcher(full).find()) interesting.putIfAbsent(f, "class");
            else if (FN != null && FN.matcher(f.getName()).find()) interesting.putIfAbsent(f, "fname");
        }

        out.println("\n======== INTERESTING FUNCTIONS (" + interesting.size() + ") ========");
        for (Map.Entry<Function,String> e : interesting.entrySet())
            out.println(String.format("%-10s %-10s %s", e.getKey().getEntryPoint(), e.getValue(), e.getKey().getName(true)));

        // ---- decompile ----
        DecompInterface dec = new DecompInterface();
        dec.toggleCCode(true);
        dec.openProgram(p);
        int n = 0;
        for (Function f : interesting.keySet()) {
            if (n++ >= cap) { out.println("\n// [capped at " + cap + "]"); break; }
            out.println("\n\n//======================================================");
            out.println("// " + f.getName(true) + "   @ " + f.getEntryPoint() + "   (" + interesting.get(f) + ")");
            out.println("//======================================================");
            try {
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null)
                    out.println(r.getDecompiledFunction().getC());
                else
                    out.println("// <decompile failed>");
            } catch (Exception ex) { out.println("// <ex: " + ex + ">"); }
        }
        dec.dispose();
        out.close();
        println("DumpGrep: wrote " + outPath + " (" + interesting.size() + " funcs)");
    }

    static Pattern pat(String s) {
        return (s == null || s.isEmpty()) ? null
               : Pattern.compile("(?i)(" + s.replace('~', '|') + ")");
    }
}
