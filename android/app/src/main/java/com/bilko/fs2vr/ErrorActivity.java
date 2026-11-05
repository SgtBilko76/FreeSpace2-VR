package com.bilko.fs2vr;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * 2D panel for a fatal engine error (FS2Activity.showFatalError). Runs in its own process
 * (":error" in the manifest) because the game process exits right after starting it. Inside VR the
 * engine's own message box is invisible: the player only heard a beep and the game was gone.
 * Saves the error and the engine log to /sdcard/FS2 so players can send them in.
 */
public class ErrorActivity extends Activity {
    static final String EXTRA_TEXT = "text";
    private static final String TAG = "FS2VR";

    private TextView mStatus;
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        String text = getIntent().getStringExtra(EXTRA_TEXT);
        if (text == null) text = "(no message)";
        String saved = saveReport(text);

        float dp = getResources().getDisplayMetrics().density;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (28 * dp);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(getString(R.string.app_name) + ": the game stopped with an error");
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);

        TextView msg = new TextView(this);
        msg.setText(firstLines(text, 14));
        msg.setTextSize(15);
        msg.setTypeface(Typeface.MONOSPACE);
        msg.setPadding(0, (int) (14 * dp), 0, (int) (14 * dp));
        msg.setTextIsSelectable(true);
        root.addView(msg);

        TextView hint = new TextView(this);
        hint.setTextSize(17);
        hint.setText("Errors while the game starts usually mean the FreeSpace 2 files in /sdcard/FS2 are incomplete "
                + "or damaged, for example a copy that did not finish or ran out of space. Copy the game files "
                + "(the .vp files) from your PC again, then press Start again.\n\n"
                + (saved != null ? saved : "")
                + "If it keeps happening, please report it with these files.");
        root.addView(hint);

        mStatus = new TextView(this);
        mStatus.setTextSize(17);
        mStatus.setPadding(0, (int) (10 * dp), 0, (int) (10 * dp));
        root.addView(mStatus);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_HORIZONTAL);
        button(row, "Start again", v -> restart());
        button(row, "Close", v -> finishAndRemoveTask());
        root.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(root);
        setContentView(scroll);
    }

    private Button button(LinearLayout row, String label, android.view.View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(17);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(12, 0, 12, 0);
        row.addView(b, lp);
        return b;
    }

    /** Starts the game again in the app's main process. */
    private void restart() {
        Intent i = new Intent(this, FS2Activity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
        finishAndRemoveTask();
        // leave this process: the game must start in the app's main process
        mHandler.postDelayed(() -> android.os.Process.killProcess(android.os.Process.myPid()), 1500);
    }

    private static String firstLines(String text, int max) {
        String[] lines = text.split("\n");
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < lines.length && i < max; i++) sb.append(lines[i]).append('\n');
        if (lines.length > max) sb.append("...");
        return sb.toString().trim();
    }

    /** Writes /sdcard/FS2/error.txt and a copy of the engine log; returns a sentence for the panel. */
    private String saveReport(String text) {
        try {
            File dir = new File(FS2Activity.GAME_DATA_DIR);
            dir.mkdirs();
            try (OutputStream out = new FileOutputStream(new File(dir, "error.txt"))) {
                out.write(text.getBytes(StandardCharsets.UTF_8));
            }
            File log = new File(getFilesDir(), "data/fs2_open.log");
            if (log.isFile()) {
                try (InputStream in = new FileInputStream(log);
                     OutputStream out = new FileOutputStream(new File(dir, "fs2_open.log"))) {
                    byte[] b = new byte[1 << 16];
                    int n;
                    while ((n = in.read(b)) > 0) out.write(b, 0, n);
                }
                return "The error and the game log were saved as /sdcard/FS2/error.txt and /sdcard/FS2/fs2_open.log "
                        + "(SideQuest: File browser). ";
            }
            return "The error was saved as /sdcard/FS2/error.txt (SideQuest: File browser). ";
        } catch (Exception e) {
            Log.w(TAG, "could not save the error report", e);
            return null;
        }
    }
}
