import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

// Registers a FID database with the application so the "Function ID" analyzer queries it.
// Run as a -preScript on import: the analyzer reads the attached set when it runs, so a
// binary imported this way comes out of auto-analysis with its CRT already named.
//
// The registration persists in Ghidra's user preferences, so re-attaching is a no-op; it is
// re-asserted every run because the preference file is local state, not a project artifact.
//
// Args (key=value, space separated):
//   fidb=<path>   the .fidb to attach
public class AttachFid extends GhidraScript {

    public void run() throws Exception {
        // analyzeHeadless splits a key=value argument on the '=' before the script sees
        // it, so a pair may arrive as one token or as two; accept both.
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String fidbPath = a.get("fidb");
        if (fidbPath == null) {
            printerr("AttachFid: fidb= is required");
            return;
        }
        File fidb = new File(fidbPath);
        if (!fidb.isFile()) {
            printerr("AttachFid: no such FID database: " + fidb);
            return;
        }
        FidFile fidFile = FidFileManager.getInstance().addUserFidFile(fidb);
        if (fidFile == null) {
            printerr("AttachFid: not a usable FID database: " + fidb);
            return;
        }
        println("AttachFid: attached " + fidFile.getPath() + " (active=" + fidFile.isActive() + ")");
    }
}
