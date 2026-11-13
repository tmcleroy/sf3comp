// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp.
package io.github.tmcleroy.sf3comp;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

// The collection's launcher (libsf3comp-launcher.so: recomp-common's launcher). What a launcher does elsewhere by
// starting a program, it asks this activity for, from its own thread (recomp-common/launcher/src/game_process.cpp):
// startGame() starts a game's activity in the next process slot, and gameResult() says how it ended; importFiles()
// asks the player for their ROM sets with the system's picker and copies them where the games can open them, and
// importResult() says where; installPackage() hands a new version's APK, downloaded and checked, to Android's
// installer (recomp-common/launcher/src/update.cpp).
public class LauncherActivity extends SDLActivity {
    static final int STILL_RUNNING = Integer.MIN_VALUE;
    private static final Class<?>[] SLOTS = {
        GameActivity.Slot0.class, GameActivity.Slot1.class, GameActivity.Slot2.class, GameActivity.Slot3.class};
    private static final int REQUEST_GAME = 1;
    private static final int REQUEST_IMPORT = 2;
    private static final int REQUEST_INSTALLS = 3;

    private int nextSlot = 0;
    private volatile int result = 0;
    private volatile String imported = "";
    private String importFolder;
    private volatile String pendingInstall;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "sf3comp-launcher"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        unpackRomSets();
        super.onCreate(savedInstanceState);
    }

    // A development build's own ROM sets (tools/release/android.sh, ROM_SETS: assets/roms/), copied into the launcher's
    // folder for them as Add ROM sets... would, and each game of roms/games.txt set up to find its set there
    private void unpackRomSets() {
        try {
            String[] files = getAssets().list("roms");
            if (files == null || files.length == 0) {
                return;
            }
            File home = new File(getExternalFilesDir(null), getString(R.string.app_name));
            File roms = new File(home, "roms");
            roms.mkdirs();
            for (String name : files) {
                if (!name.endsWith(".zip") || new File(roms, name).exists()) {
                    continue;
                }
                File partial = new File(roms, name + ".part");
                try (InputStream in = getAssets().open("roms/" + name); OutputStream out = new FileOutputStream(partial)) {
                    byte[] buffer = new byte[1 << 16];
                    for (int got; (got = in.read(buffer)) > 0;) {
                        out.write(buffer, 0, got);
                    }
                }
                partial.renameTo(new File(roms, name));
            }
            // (launcher.ini: a game's set is its section's rom; one already chosen is kept)
            File ini = new File(home, "launcher.ini");
            String text = ini.exists() ? new String(Files.readAllBytes(ini.toPath()), StandardCharsets.UTF_8) : "";
            StringBuilder more = new StringBuilder();
            try (BufferedReader games = new BufferedReader(new InputStreamReader(getAssets().open("roms/games.txt"), StandardCharsets.UTF_8))) {
                for (String id; (id = games.readLine()) != null;) {
                    if (!id.isEmpty() && !text.contains("[game." + id + "]")) {
                        more.append("\n[game.").append(id).append("]\nrom=").append(roms.getPath()).append('\n');
                    }
                }
            }
            if (more.length() > 0) {
                try (FileWriter out = new FileWriter(ini, true)) {
                    out.write(more.toString());
                }
            }
        } catch (Exception e) {
            Log.w("sf3comp", "the packed ROM sets: " + e);
        }
    }

    // Starts the game whose library is lib<library>.so, with these arguments, in its folder, its output to log
    public boolean startGame(String library, String[] arguments, String folder, String log) {
        final Intent intent = new Intent(this, SLOTS[nextSlot]);
        nextSlot = (nextSlot + 1) % SLOTS.length;
        intent.putExtra(GameActivity.EXTRA_LIBRARY, library);
        intent.putExtra(GameActivity.EXTRA_ARGUMENTS, arguments);
        intent.putExtra(GameActivity.EXTRA_FOLDER, folder);
        intent.putExtra(GameActivity.EXTRA_LOG, log);
        result = STILL_RUNNING;
        runOnUiThread(() -> startActivityForResult(intent, REQUEST_GAME));
        return true;
    }

    // An HTTP request, from curl's arguments (recomp-common/launcher/src/run_program.cpp): Curl.java
    public String curl(String[] arguments) {
        return Curl.run(arguments);
    }

    // A new version's APK (in the app's cache: UpdateProvider) opened with Android's installer, which asks the player
    // and installs it over this app: 0. Android lets an app open the installer only once the player allows it to
    // install apps, so the first time Android's Settings for that open instead, and the installer after them: 1.
    // -1: neither could be opened.
    public int installPackage(String path) {
        final File apk = new File(path);
        if (UpdateProvider.uri(this, apk) == null || !apk.isFile()) {
            return -1;
        }
        if (!getPackageManager().canRequestPackageInstalls()) {
            pendingInstall = path;
            final Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()));
            runOnUiThread(() -> {
                try {
                    startActivityForResult(settings, REQUEST_INSTALLS);
                } catch (Exception e) {
                    Log.w("sf3comp", "no Settings for installing apps: " + e);
                }
            });
            return 1;
        }
        return openInstaller(apk) ? 0 : -1;
    }

    private boolean openInstaller(File apk) {
        final Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(UpdateProvider.uri(this, apk), UpdateProvider.MIME);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        if (intent.resolveActivity(getPackageManager()) == null) {
            Log.w("sf3comp", "no installer for " + apk);
            return false;
        }
        runOnUiThread(() -> {
            try {
                startActivity(intent);
            } catch (Exception e) {
                Log.w("sf3comp", "the installer: " + e);
            }
        });
        return true;
    }

    // How the game ended: its main()'s value (-1: some other way), or STILL_RUNNING
    public int gameResult() {
        return result;
    }

    // The system's picker for files (several if `many`), each chosen copied into `folder` by its own name
    public void importFiles(String folder, boolean many) {
        importFolder = folder;
        imported = null;
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, many);
        runOnUiThread(() -> startActivityForResult(intent, REQUEST_IMPORT));
    }

    // The copies' paths, one a line ("" for none), or null while the player chooses and they are copied
    public String importResult() {
        return imported;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_INSTALLS) {
            // (back from Settings: the installer, if the player allowed it; otherwise the update card's Install asks again)
            String path = pendingInstall;
            pendingInstall = null;
            if (path != null && getPackageManager().canRequestPackageInstalls()) {
                openInstaller(new File(path));
            }
        } else if (requestCode == REQUEST_GAME) {
            result = resultCode >= RESULT_FIRST_USER ? resultCode - RESULT_FIRST_USER : -1;
        } else if (requestCode == REQUEST_IMPORT) {
            final List<Uri> chosen = new ArrayList<>();
            if (resultCode == RESULT_OK && data != null) {
                if (data.getClipData() != null) {
                    for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                        chosen.add(data.getClipData().getItemAt(i).getUri());
                    }
                } else if (data.getData() != null) {
                    chosen.add(data.getData());
                }
            }
            // (a set is tens of megabytes: copied away from the activity's thread)
            new Thread(() -> {
                StringBuilder paths = new StringBuilder();
                for (Uri uri : chosen) {
                    String path = copy(uri, importFolder);
                    if (path != null) {
                        paths.append(path).append('\n');
                    }
                }
                imported = paths.toString();
            }).start();
        }
    }

    // A document copied into a folder by its own name; its path there, or null
    private String copy(Uri uri, String folder) {
        String name = null;
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w("sf3comp", "no name for " + uri + ": " + e);
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment();
        }
        name = new File(name).getName();  // (never a path)
        File to = new File(folder, name);
        File partial = new File(folder, name + ".part");
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(partial)) {
            byte[] buffer = new byte[1 << 16];
            for (int got; (got = in.read(buffer)) > 0;) {
                out.write(buffer, 0, got);
            }
        } catch (Exception e) {
            Log.w("sf3comp", "could not copy " + uri + ": " + e);
            partial.delete();
            return null;
        }
        to.delete();
        return partial.renameTo(to) ? to.getPath() : null;
    }
}
