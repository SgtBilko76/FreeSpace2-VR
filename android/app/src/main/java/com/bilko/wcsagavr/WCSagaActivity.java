package com.bilko.wcsagavr;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * Wing Commander Saga launcher. The FreeSpace Open engine runs in
 * libfs2_open_*.so (android_main); this class supplies the library list,
 * the data folder (queried by the engine through getWorkingFolder) and the
 * command line.
 */
public class WCSagaActivity extends SDLActivity {
    private static final String TAG = "WCSaga";

    /** Folder that contains the Wing_Commander_Saga mod folder. */
    public static final String GAME_DATA_DIR = "/sdcard/WCSaga/";
    public static final String MOD = "Wing_Commander_Saga";

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

    @Override
    protected String[] getArguments() {
        return new String[] { "-mod", MOD };
    }
}
