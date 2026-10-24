package com.bilko.fs2vr;

import android.Manifest;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
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
    private static final int STORAGE_REQUEST = 1;

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
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception e2) {
                    // some headset OS builds have no such page: fall back to the app's settings
                    try {
                        startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                Uri.parse("package:" + getPackageName())));
                    } catch (Exception e3) {
                        Log.e(TAG, "could not open the all-files access settings", e3);
                    }
                }
            }
        } else if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R
                && checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                        != PackageManager.PERMISSION_GRANTED) {
            // Android 10 (PICO OS 5): no all-files access, ask for storage instead.
            Log.w(TAG, "storage permission missing; asking the user to grant it");
            requestPermissions(new String[] {
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE }, STORAGE_REQUEST);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != STORAGE_REQUEST)
            return;
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            // The engine already looked for its data without access: start over.
            Log.i(TAG, "storage permission granted, restarting");
            Intent restart = getPackageManager().getLaunchIntentForPackage(getPackageName());
            if (restart != null) {
                restart.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                PendingIntent pending = PendingIntent.getActivity(this, 0, restart,
                        PendingIntent.FLAG_CANCEL_CURRENT | PendingIntent.FLAG_IMMUTABLE);
                AlarmManager alarm = (AlarmManager) getSystemService(Context.ALARM_SERVICE);
                alarm.set(AlarmManager.RTC, System.currentTimeMillis() + 500, pending);
            }
            android.os.Process.killProcess(android.os.Process.myPid());
        } else {
            Log.w(TAG, "storage permission denied");
        }
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
