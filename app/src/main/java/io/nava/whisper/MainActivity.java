package io.nava.whisper;

import android.app.NativeActivity;
import android.content.ContentUris;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.provider.Settings;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public class MainActivity extends NativeActivity {

    // NativeActivity loads the native lib via the "android.app.lib_name" manifest
    // meta-data, but that load path does NOT register the library for resolving
    // explicitly-declared `native` methods on this Java class. Without this
    // explicit load, nativeOnModelPicked() throws UnsatisfiedLinkError at runtime
    // ("No implementation found ... is the library loaded?"), which
    // persistAndNotify() silently swallows — so a picked model is saved to prefs
    // but the running process is never told to load it, leaving the UI stuck on
    // "LOADING" until the app is restarted.
    static {
        System.loadLibrary("whisper_android");
    }

    private static final int    REQUEST_PICK_MODEL = 42;
    private static final String PREFS_NAME         = "whisper_prefs";
    private static final String PREF_MODEL_PATH    = "model_path";

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (!Environment.isExternalStorageManager()) {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
        }
        handleLoadModelIntent(getIntent());
    }

    // Called from C++ via JNI to open the file picker. Prefers FileX; falls back to system picker.
    public void launchFilePicker() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.setPackage("io.nava.filex");
        if (getPackageManager().resolveActivity(intent, 0) != null) {
            startActivityForResult(intent, REQUEST_PICK_MODEL);
            return;
        }
        Intent fallback = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        fallback.addCategory(Intent.CATEGORY_OPENABLE);
        fallback.setType("*/*");
        startActivityForResult(fallback, REQUEST_PICK_MODEL);
    }

    // Called from C++ via JNI on first init to get the saved model path (empty = none).
    public String getSavedModelPath() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        return prefs.getString(PREF_MODEL_PATH, "");
    }

    // Called from C++ via JNI when the saved path fails to load — clears it so we don't retry.
    public void clearSavedModelPath() {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().remove(PREF_MODEL_PATH).apply();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_MODEL || resultCode != RESULT_OK || data == null) return;

        Uri uri = data.getData();
        if (uri == null) return;

        // Try to get a direct filesystem path first (no copy needed).
        String path = resolveRealPath(uri);
        if (path != null && !path.isEmpty()) {
            persistAndNotify(path);
            return;
        }

        // Fall back: copy to internal storage on a background thread.
        String displayName = queryDisplayName(uri);
        if (displayName == null) displayName = "model.bin";
        final String fileName = displayName;

        new Thread(() -> {
            String copied = copyToInternal(uri, fileName);
            if (copied != null) {
                mainHandler.post(() -> persistAndNotify(copied));
            }
        }).start();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleLoadModelIntent(intent);
    }

    private void handleLoadModelIntent(Intent intent) {
        if (intent == null) return;
        if (!"io.nava.whisper.LOAD_MODEL".equals(intent.getAction())) return;

        String path = intent.getStringExtra("model_path");
        if (path == null || path.isEmpty()) {
            Uri uri = intent.getData();
            if (uri != null) path = uri.getPath();
        }
        if (path == null || path.isEmpty()) return;
        persistAndNotify(path);
    }

    private void persistAndNotify(String path) {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .edit().putString(PREF_MODEL_PATH, path).apply();
        try {
            nativeOnModelPicked(path);
        } catch (UnsatisfiedLinkError e) {
            android.util.Log.e("WhisperMain", "nativeOnModelPicked unresolved: " + e);
        }
    }

    // Called from C++ via JNI when the COPY button is tapped.
    public void copyToClipboard(String text) {
        mainHandler.post(() -> {
            android.content.ClipboardManager cm =
                    (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (cm != null) {
                cm.setPrimaryClip(
                        android.content.ClipData.newPlainText("transcription", text));
            }
        });
    }

    private String resolveRealPath(Uri uri) {
        if ("file".equals(uri.getScheme())) return uri.getPath();
        if (!"content".equals(uri.getScheme())) return null;

        if (DocumentsContract.isDocumentUri(this, uri)) {
            String docId = DocumentsContract.getDocumentId(uri);
            String authority = uri.getAuthority();

            if ("com.android.externalstorage.documents".equals(authority)) {
                String[] split = docId.split(":");
                if (split.length == 2 && "primary".equalsIgnoreCase(split[0])) {
                    return Environment.getExternalStorageDirectory() + "/" + split[1];
                }
            }

            if ("com.android.providers.downloads.documents".equals(authority)) {
                try {
                    Uri contentUri = ContentUris.withAppendedId(
                            Uri.parse("content://downloads/public_downloads"),
                            Long.parseLong(docId));
                    return queryMediaStorePath(contentUri);
                } catch (NumberFormatException ignored) {}
            }

            if ("com.android.providers.media.documents".equals(authority)) {
                String[] split = docId.split(":");
                if (split.length == 2) {
                    Uri mediaUri = MediaStore.Files.getContentUri("external");
                    return queryMediaStorePath(ContentUris.withAppendedId(mediaUri,
                            Long.parseLong(split[1])));
                }
            }
        }

        return queryMediaStorePath(uri);
    }

    private String queryMediaStorePath(Uri uri) {
        try (Cursor c = getContentResolver().query(uri,
                new String[]{"_data"}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String path = c.getString(0);
                if (path != null && !path.isEmpty()) return path;
            }
        } catch (Exception ignored) {}
        return null;
    }

    private String copyToInternal(Uri uri, String fileName) {
        File modelsDir = new File(getFilesDir(), "models");
        //noinspection ResultOfMethodCallIgnored
        modelsDir.mkdirs();
        File dest = new File(modelsDir, fileName);

        if (dest.exists() && dest.length() > 0) return dest.getAbsolutePath();

        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
            if (pfd == null) return null;
            try (FileInputStream  in  = new FileInputStream(pfd.getFileDescriptor());
                 FileOutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[1 << 20];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
            pfd.close();
            return dest.getAbsolutePath();
        } catch (Exception e) {
            android.util.Log.e("WhisperMain", "copy failed: " + e);
            dest.delete();
            return null;
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {}
        return null;
    }

    private static native void nativeOnModelPicked(String path);
}
