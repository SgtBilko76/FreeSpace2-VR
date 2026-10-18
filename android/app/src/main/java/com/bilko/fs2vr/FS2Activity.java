package com.bilko.fs2vr;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * FreeSpace 2 launcher. The FreeSpace Open engine runs in
 * libfs2_open_*.so (android_main); this class supplies the library list,
 * the data folder (queried by the engine through getWorkingFolder) and the
 * command line.
 */
public class FS2Activity extends SDLActivity {
    private static final String TAG = "FS2VR";

    /** Folder with the retail FreeSpace 2 .vp files (and data/movies). */
    public static final String GAME_DATA_DIR = "/sdcard/FS2/";

    /* Called by FSO (osapi.cpp, os_android_get_working_folder_path). */
    public static String getWorkingFolder() { return GAME_DATA_DIR; }

    /* Touch overlay hooks expected by FSO; the Quest has no touch overlay. */
    public static void enableOverlay() { }
    public static void disableOverlay() { }
    public static void setOverlayOpacity(float opacity) { }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            Log.w(TAG, "all-files access missing; asking the user to grant it");
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            } catch (Exception e) {
                Log.e(TAG, "could not open the all-files access settings", e);
            }
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", BuildConfigLib.ENGINE };
    }

    @Override
    protected String getMainFunction() {
        return "android_main";
    }

    /**
     * A copied GOG/Steam install keeps the cutscenes (*.mve) in data2/ and data3/ (CD 2/3);
     * the engine looks for them in data/movies. Move them there (same storage, so instant).
     */
    private static void moveMoviesIntoPlace() {
        java.io.File base = new java.io.File(GAME_DATA_DIR);
        java.io.File movies = new java.io.File(base, "data/movies");
        for (String d : new String[] { ".", "data2", "data3" }) {
            java.io.File[] files = new java.io.File(base, d).listFiles();
            if (files == null)
                continue;
            for (java.io.File f : files) {
                String name = f.getName().toLowerCase(java.util.Locale.ROOT);
                if (!f.isFile() || !name.endsWith(".mve"))
                    continue;
                java.io.File dst = new java.io.File(movies, name);
                if (dst.exists())
                    continue;
                movies.mkdirs();
                Log.i(TAG, "moving " + f + " -> " + dst + ": " + f.renameTo(dst));
            }
        }
    }

    @Override
    protected String[] getArguments() {
        try {
            moveMoviesIntoPlace();
        } catch (Exception e) {
            Log.e(TAG, "could not move the movies", e);
        }
        java.util.ArrayList<String> args = new java.util.ArrayList<>(java.util.Arrays.asList("-vr"));
        // Extra engine arguments for testing, e.g.
        //   adb shell am start -n com.bilko.fs2vr/.FS2Activity --es fsoArgs "-pilot Bilko -start_mission SM1-01.fs2"
        Intent i = getIntent();
        String extra = (i != null) ? i.getStringExtra("fsoArgs") : null;
        if (extra != null && !extra.trim().isEmpty()) {
            args.addAll(java.util.Arrays.asList(extra.trim().split("\\s+")));
        }
        Log.i(TAG, "engine arguments: " + args);
        return args.toArray(new String[0]);
    }
}
