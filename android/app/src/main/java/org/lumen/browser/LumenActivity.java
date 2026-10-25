package org.lumen.browser;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import android.util.Log;
import android.content.Context;
import android.content.Intent;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.SharedPreferences;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.widget.Toast;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.X509Certificate;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

public final class LumenActivity extends SDLActivity {
    /* the running activity for helpers outside SDLActivity's subclasses, or null */
    static SDLActivity current() { return mSingleton; }

    private static final String PROXY_EXTRA = "org.lumen.browser.PROXY";
    private static final String CONSOLE_EXTRA = "org.lumen.browser.CONSOLE";
    private static final String V8_FLAGS_EXTRA = "org.lumen.browser.V8_FLAGS";
    private static final String JS_TIMEOUT_EXTRA = "org.lumen.browser.JS_TIMEOUT_MS";
    private static final String PRE_JS_EXTRA = "org.lumen.browser.PRE_JS";
    private static final String FRAME_PRE_JS_EXTRA = "org.lumen.browser.FRAME_PRE_JS";
    private static final String TEST_URLS_EXTRA = "org.lumen.browser.TEST_URLS";
    private boolean nativeArgumentsRequested;
    private static native boolean nativeOpenUrl(byte[] url);
    private static native boolean nativeOpenFile(byte[] path, boolean viewer);
    private static native boolean nativeRequestBack();
    private static native void nativeUiAction(int code, int value, byte[] first, byte[] second);
    static native String nativeUiIcon(int code);
    static native float[] nativeTabStats();
    static native int nativeSiteCookies(byte[] url);
    static native String nativeSite(byte[] url);
    private static native void nativeFilesPicked(byte[] paths);
    private static final int PICK_REQUEST = 0x4c50;
    private TouchFeedback touchFeedback;
    private Runnable unregisterBack;
    private BrowserSurface browserSurface;
    private BrowserToolbar toolbar;

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        browserSurface = new BrowserSurface(context, rotation -> {
            if (mCurrentRotation != rotation) {
                mCurrentRotation = rotation;
                onNativeRotationChanged(rotation);
            }
        });
        return browserSurface;
    }

    private synchronized void requestBack() {
        if (toolbar != null && toolbar.editing()) { toolbar.stopEditing(); return; }
        if (!nativeArgumentsRequested || mBrokenLibraries) {
            moveTaskToBack(true);
        } else if (!nativeRequestBack()) {
            Toast.makeText(this, "Unable to go back: browser is busy", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!mBrokenLibraries && mLayout != null) {
            BrowserInsets.install(getWindow(), mLayout);
            toolbar = new BrowserToolbar(this, UI_ACTION, () -> { if (mSurface != null) mSurface.requestFocus(); });
            mLayout.addView(toolbar, new android.widget.RelativeLayout.LayoutParams(0, 0));
            getWindow().getDecorView().getViewTreeObserver().addOnGlobalFocusChangeListener((old, now) -> toolbar.focusMoved(now));
        }
        if (Build.VERSION.SDK_INT >= 33) unregisterBack = Api33.registerBack(this);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == PICK_REQUEST) {   /* reading the files may take a while: off the UI thread */
            if (resultCode != RESULT_OK || data == null) { nativeFilesPicked(null); return; }
            new Thread(() -> {
                String paths = SharedFiles.picked(this, data);
                nativeFilesPicked(paths != null ? paths.getBytes(StandardCharsets.UTF_8) : null);
            }, "LumenPickFiles").start();
            return;
        }
        if (!ScreenCapture.result(this, requestCode, resultCode, data)) super.onActivityResult(requestCode, resultCode, data);
    }

    /* the system's document picker for an <input type=file>, from the native thread: accept's MIME
       types and extensions narrow it; the answer goes to nativeFilesPicked */
    public static void pickFiles(byte[] accept, boolean multiple) {
        final SDLActivity activity = mSingleton;
        if (activity == null) { nativeFilesPicked(null); return; }
        java.util.LinkedHashSet<String> types = new java.util.LinkedHashSet<>();
        for (String token : new String(accept != null ? accept : new byte[0], StandardCharsets.UTF_8).split(",")) {
            String t = token.trim().toLowerCase(java.util.Locale.ROOT), type = t.startsWith(".")
                ? android.webkit.MimeTypeMap.getSingleton().getMimeTypeFromExtension(t.substring(1)) : t.indexOf('/') > 0 ? t : null;
            if (t.isEmpty()) continue;
            if (type == null) { types.clear(); break; }   /* an extension Android does not know: show everything */
            types.add(type);
        }
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType(types.size() == 1 ? types.iterator().next() : "*/*");
        if (types.size() > 1) pick.putExtra(Intent.EXTRA_MIME_TYPES, types.toArray(new String[0]));
        pick.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
        activity.runOnUiThread(() -> {
            try {
                activity.startActivityForResult(pick, PICK_REQUEST);
            } catch (RuntimeException exception) {
                Log.e("Lumen", "Unable to open the file picker", exception);
                nativeFilesPicked(null);
            }
        });
    }

    @Override
    public void onBackPressed() {
        requestBack();
    }

    /* The page's text input stopping hides the keyboard and focuses the page; while the address
       field is being edited only SDL's own state is updated. */
    @Override
    protected boolean sendCommand(int command, Object data) {
        if (command != COMMAND_TEXTEDIT_HIDE || toolbar == null) return super.sendCommand(command, data);
        runOnUiThread(() -> {
            if (!toolbar.editing()) { LumenActivity.super.sendCommand(command, data); return; }
            if (mTextEdit != null) mTextEdit.setLayoutParams(new android.widget.RelativeLayout.LayoutParams(0, 0));
            onNativeScreenKeyboardHidden();
        });
        return true;
    }

    /* a touch on the page ends address editing first */
    @Override
    public boolean dispatchTouchEvent(android.view.MotionEvent event) {
        if (touchFeedback == null) touchFeedback = TouchFeedback.on(getWindow().getDecorView());
        touchFeedback.touch(event);
        if (toolbar != null) toolbar.indicatorTouch(event);
        if (event.getActionMasked() == android.view.MotionEvent.ACTION_DOWN && toolbar != null && toolbar.editing() && toolbar.outside(event)) toolbar.stopEditing();
        return super.dispatchTouchEvent(event);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (toolbar != null) toolbar.pause(true);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (toolbar != null) toolbar.pause(false);
    }

    @Override
    protected void onDestroy() {
        if (toolbar != null) toolbar.pause(true);
        if (browserSurface != null) browserSurface.close();
        if (unregisterBack != null) {
            unregisterBack.run();
            unregisterBack = null;
        }
        super.onDestroy();
    }

    @android.annotation.TargetApi(33)
    private static final class Api33 {
        static Runnable registerBack(LumenActivity activity) {
            android.window.OnBackInvokedDispatcher dispatcher = activity.getOnBackInvokedDispatcher();
            android.window.OnBackInvokedCallback callback = activity::requestBack;
            dispatcher.registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback);
            return () -> dispatcher.unregisterOnBackInvokedCallback(callback);
        }
    }

    private String launchUrl(Intent intent) {
        return intent != null && Intent.ACTION_VIEW.equals(intent.getAction())
            ? LaunchUrl.sanitize(intent.getDataString()) : null;
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String url;
        synchronized (this) {
            setIntent(intent);
            if (!nativeArgumentsRequested || mBrokenLibraries) return;
            if (openShared(intent)) return;
            url = launchUrl(intent);
            if (url == null) return;
        }
        if (!nativeOpenUrl(url.getBytes(StandardCharsets.UTF_8))) {
            Toast.makeText(this, "Unable to open link: browser is busy", Toast.LENGTH_SHORT).show();
        }
    }

    /* "Open with" and "Share" from other apps: files, and links in shared text */
    private boolean openShared(Intent intent) {
        return SharedFiles.handle(this, intent,
            (path, viewer) -> nativeOpenFile(path.getBytes(StandardCharsets.UTF_8), viewer),
            url -> { if (!nativeOpenUrl(url.getBytes(StandardCharsets.UTF_8))) runOnUiThread(() ->
                Toast.makeText(this, "Unable to open link: browser is busy", Toast.LENGTH_SHORT).show()); });
    }

    /* the system share sheet for UTF-8 text, from the native thread */
    public static void shareText(byte[] text) {
        final SDLActivity activity = mSingleton;
        if (activity == null || text == null) return;
        final String value = new String(text, StandardCharsets.UTF_8);
        activity.runOnUiThread(() -> {
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("text/plain");
            send.putExtra(Intent.EXTRA_TEXT, value);
            try {
                activity.startActivity(Intent.createChooser(send, null));
            } catch (RuntimeException exception) {
                Log.e("Lumen", "Unable to share", exception);
            }
        });
    }

    /* from the native thread: a page item's menu */
    public static void showListMenu(byte[] title, byte[][] items) {
        final SDLActivity activity = mSingleton;
        if (activity == null || title == null || items == null) return;
        final String heading = new String(title, StandardCharsets.UTF_8);
        final String[] labels = utf8(items);
        activity.runOnUiThread(() -> BrowserMenus.showListMenu(activity, heading, labels, UI_ACTION));
    }

    /* from the native thread: an image file under cache/clip becomes the primary clip (ClipProvider serves it) */
    public static void clipImageFile(byte[] path) {
        final SDLActivity activity = mSingleton;
        if (activity == null || path == null) return;
        final java.io.File file = new java.io.File(new String(path, StandardCharsets.UTF_8));
        activity.runOnUiThread(() -> {
            try {
                android.content.ClipboardManager clipboard = (android.content.ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
                if (clipboard != null) clipboard.setPrimaryClip(android.content.ClipData.newUri(activity.getContentResolver(), "Image", ClipProvider.uri(activity, file)));
            } catch (RuntimeException exception) {
                Log.w("Lumen", "Unable to copy the image", exception);
            }
        });
    }

    /* from the native thread: the system share sheet for an image file under cache/clip */
    public static void shareImageFile(byte[] path) {
        final SDLActivity activity = mSingleton;
        if (activity == null || path == null) return;
        final java.io.File file = new java.io.File(new String(path, StandardCharsets.UTF_8));
        activity.runOnUiThread(() -> {
            Uri uri = ClipProvider.uri(activity, file);
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("image/png");
            send.putExtra(Intent.EXTRA_STREAM, uri);
            send.setClipData(android.content.ClipData.newRawUri("Image", uri));
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try {
                activity.startActivity(Intent.createChooser(send, null));
            } catch (RuntimeException exception) {
                Log.e("Lumen", "Unable to share the image", exception);
            }
        });
    }

    /* from the native thread on paste: the primary clip's first image as {MIME type, bytes} (at most 32 MB), else null */
    public static byte[][] clipboardImage() {
        final SDLActivity activity = mSingleton;
        if (activity == null) return null;
        try {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
            android.content.ClipData clip = clipboard != null ? clipboard.getPrimaryClip() : null;
            if (clip == null) return null;
            ContentResolver resolver = activity.getContentResolver();
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                String type = uri != null ? resolver.getType(uri) : null;
                if (type == null || !type.startsWith("image/")) continue;
                try (java.io.InputStream in = resolver.openInputStream(uri)) {
                    if (in == null) continue;
                    java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
                    byte[] buffer = new byte[65536];
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                        if (out.size() > 32 << 20) return null;
                    }
                    return new byte[][] { type.getBytes(StandardCharsets.UTF_8), out.toByteArray() };
                }
            }
        } catch (Exception exception) {
            Log.w("Lumen", "Unable to read the clipboard image", exception);
        }
        return null;
    }

    /* from the native thread while a voice microphone is open (getUserMedia with echo cancellation):
       like Chrome's WebRTC, the call runs in communication mode, so the platform echo canceller
       pairs the microphone with the call's playback, on the loudspeaker unless a headset is connected */
    public static void setVoiceCall(boolean on) {
        final SDLActivity activity = mSingleton;
        if (activity != null) VoiceCall.set(activity, on);
    }

    /* getDisplayMedia from the native thread (src/media/screen.c) */
    public static void screenStart(int maxWidth, int maxHeight, int fps, boolean audio) { ScreenCapture.start(maxWidth, maxHeight, fps, audio); }
    public static void screenStop() { ScreenCapture.stop(); }

    private static final class VoiceCall {
        private static boolean active, speakerBefore;
        private static int modeBefore;

        static synchronized void set(SDLActivity activity, boolean on) {
            if (on == active) return;
            AudioManager audio = (AudioManager) activity.getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
            if (audio == null) return;
            try {
                if (on) {
                    modeBefore = audio.getMode();
                    audio.setMode(AudioManager.MODE_IN_COMMUNICATION);
                    if (Build.VERSION.SDK_INT >= 31) Api31.route(audio);
                    else {
                        speakerBefore = audio.isSpeakerphoneOn();
                        audio.setSpeakerphoneOn(!wiredHeadset(audio));
                    }
                } else {
                    if (Build.VERSION.SDK_INT >= 31) audio.clearCommunicationDevice();
                    else audio.setSpeakerphoneOn(speakerBefore);
                    audio.setMode(modeBefore == AudioManager.MODE_IN_COMMUNICATION ? AudioManager.MODE_NORMAL : modeBefore);
                }
                active = on;
            } catch (RuntimeException exception) {
                Log.w("Lumen", "Unable to change the voice call audio mode", exception);
                return;
            }
            activity.runOnUiThread(() -> activity.setVolumeControlStream(on ? AudioManager.STREAM_VOICE_CALL : AudioManager.USE_DEFAULT_STREAM_TYPE));
        }

        static boolean headset(int type) {
            return type == AudioDeviceInfo.TYPE_WIRED_HEADSET || type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES
                || type == AudioDeviceInfo.TYPE_USB_HEADSET || type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                || (Build.VERSION.SDK_INT >= 31 && type == AudioDeviceInfo.TYPE_BLE_HEADSET);
        }

        static boolean wiredHeadset(AudioManager audio) {
            for (AudioDeviceInfo device : audio.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                int type = device.getType();
                if (headset(type) && type != AudioDeviceInfo.TYPE_BLUETOOTH_SCO) return true;
            }
            return false;
        }
    }

    @android.annotation.TargetApi(31)
    private static final class Api31 {
        static void route(AudioManager audio) {
            AudioDeviceInfo pick = null;
            for (AudioDeviceInfo device : audio.getAvailableCommunicationDevices()) {
                if (VoiceCall.headset(device.getType())) {
                    pick = device;
                    break;
                }
                if (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) pick = device;
            }
            if (pick != null) audio.setCommunicationDevice(pick);
        }
    }

    private static String utf8(byte[] value) {
        return value != null ? new String(value, StandardCharsets.UTF_8) : null;
    }
    private static byte[] bytes(String value) {
        return value != null ? value.getBytes(StandardCharsets.UTF_8) : null;
    }
    private static String[] utf8(byte[][] values) {
        String[] out = new String[values != null ? values.length : 0];
        for (int index = 0; index < out.length; index++) out[index] = values[index] != null ? utf8(values[index]) : "";
        return out;
    }
    private static final BrowserMenus.Action UI_ACTION =
        (code, value, first, second) -> nativeUiAction(code, value, bytes(first), bytes(second));

    /* the browser menus, asked for by the native thread and shown on the UI thread */
    public static void showToast(byte[] text) {
        final SDLActivity activity = mSingleton;
        if (activity == null || text == null) return;
        final String value = utf8(text);
        activity.runOnUiThread(() -> BrowserMenus.toast(activity, value));
    }

    public static void showMenu(int[] state) {
        final SDLActivity activity = mSingleton;
        if (activity == null || state == null || state.length < 9) return;
        final int[] copy = state.clone();
        activity.runOnUiThread(() -> BrowserMenus.showMenu(activity, copy, UI_ACTION));
    }

    /* the toolbar row's state from the native thread (android_ui_toolbar) */
    public static void updateToolbar(int[] state, byte[] url) {
        final SDLActivity activity = mSingleton;
        if (!(activity instanceof LumenActivity) || state == null) return;
        final int[] copy = state.clone();
        final String address = url != null ? utf8(url) : "";
        activity.runOnUiThread(() -> {
            BrowserToolbar bar = ((LumenActivity) activity).toolbar;
            if (bar != null) bar.update(copy, address);
        });
    }

    /* the menu's Fullscreen: hides the status and navigation bars (a swipe shows them for a moment) */
    public static void setFullscreen(boolean on) {
        final SDLActivity activity = mSingleton;
        if (activity == null) return;
        activity.runOnUiThread(() -> BrowserInsets.fullscreen(activity.getWindow(), on));
    }

    public static void showBookmarks(byte[][] titles, byte[][] urls, byte[] pageTitle, byte[] pageUrl) {
        final SDLActivity activity = mSingleton;
        if (activity == null) return;
        final String[] names = utf8(titles), targets = utf8(urls);
        if (names.length != targets.length) return;
        final String title = utf8(pageTitle), url = utf8(pageUrl);
        activity.runOnUiThread(() -> BrowserMenus.showBookmarks(activity, names, targets, title, url, UI_ACTION));
    }

    public static void showWorkspaces(byte[][] names, int[] tabs, int current) {
        final SDLActivity activity = mSingleton;
        if (activity == null || tabs == null) return;
        final String[] labels = utf8(names);
        if (labels.length != tabs.length) return;
        final int[] counts = tabs.clone();
        activity.runOnUiThread(() -> BrowserMenus.showWorkspaces(activity, labels, counts, current, UI_ACTION));
    }

    public static void showTabMenu(byte[] title, byte[][] items, boolean[] checked) {
        final SDLActivity activity = mSingleton;
        if (activity == null || title == null || checked == null) return;
        final String heading = new String(title, StandardCharsets.UTF_8);
        final String[] labels = utf8(items);
        if (labels.length != checked.length) return;
        final boolean[] on = checked.clone();
        activity.runOnUiThread(() -> BrowserMenus.showTabMenu(activity, heading, labels, on, UI_ACTION));
    }

    /* Downloads go to the shared Downloads collection: a pending entry the native side writes
       through the returned descriptor, published or deleted by downloadFinish. */
    public static String[] downloadCreate(byte[] name, byte[] mime) {
        final SDLActivity activity = mSingleton;
        if (activity == null || name == null) return null;
        ContentResolver resolver = activity.getContentResolver();
        ContentValues values = new ContentValues();
        String type = mime != null ? new String(mime, StandardCharsets.UTF_8) : "";
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, new String(name, StandardCharsets.UTF_8));
        if (!type.isEmpty()) values.put(MediaStore.MediaColumns.MIME_TYPE, type);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/");
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = null;
        try {
            uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) return null;
            String shown = new String(name, StandardCharsets.UTF_8);
            try (Cursor cursor = resolver.query(uri, new String[] {MediaStore.MediaColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst() && cursor.getString(0) != null) shown = cursor.getString(0);
            }
            ParcelFileDescriptor descriptor = resolver.openFileDescriptor(uri, "rw");
            if (descriptor == null) throw new java.io.IOException("no descriptor");
            return new String[] {uri.toString(), shown, Integer.toString(descriptor.detachFd())};
        } catch (Exception exception) {
            Log.e("Lumen", "Unable to create a download", exception);
            if (uri != null) {
                try { resolver.delete(uri, null, null); } catch (RuntimeException ignored) { }
            }
            return null;
        }
    }

    public static void downloadFinish(byte[] uri, boolean keep) {
        final SDLActivity activity = mSingleton;
        if (activity == null || uri == null) return;
        Uri target = Uri.parse(new String(uri, StandardCharsets.UTF_8));
        try {
            if (keep) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.MediaColumns.IS_PENDING, 0);
                activity.getContentResolver().update(target, values, null, null);
            } else {
                activity.getContentResolver().delete(target, null, null);
            }
        } catch (RuntimeException exception) {
            Log.e("Lumen", "Unable to finish a download", exception);
        }
    }

    public static void downloadOpen(byte[] uri, byte[] mime) {
        final SDLActivity activity = mSingleton;
        if (activity == null || uri == null) return;
        final Uri target = Uri.parse(new String(uri, StandardCharsets.UTF_8));
        final String type = mime != null && mime.length > 0 ? new String(mime, StandardCharsets.UTF_8) : null;
        activity.runOnUiThread(() -> {
            Intent view = new Intent(Intent.ACTION_VIEW);
            if (type != null) view.setDataAndType(target, type); else view.setData(target);
            view.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                activity.startActivity(Intent.createChooser(view, null));
            } catch (RuntimeException exception) {
                Toast.makeText(activity, "No app can open this file", Toast.LENGTH_SHORT).show();
            }
        });
    }

    /* The Downloads page's file browser lists every file only with "All files access" (Android 11+) */
    public static boolean allFilesAccess() {
        return Build.VERSION.SDK_INT < 30 || android.os.Environment.isExternalStorageManager();
    }

    public static boolean requestAllFilesAccess() {
        final SDLActivity activity = mSingleton;
        if (activity == null || Build.VERSION.SDK_INT < 30) return false;
        activity.runOnUiThread(() -> {
            Intent own = new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + activity.getPackageName()));
            try {
                activity.startActivity(own);
            } catch (RuntimeException exception) {
                try {
                    activity.startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (RuntimeException ignored) { }
            }
        });
        return true;
    }

    /* a file of the shared storage, opened in the app the user picks: its content URI comes from the media scanner */
    public static void fileOpen(byte[] path, byte[] unused) {
        final SDLActivity activity = mSingleton;
        if (activity == null || path == null) return;
        final String file = new String(path, StandardCharsets.UTF_8);
        int dot = file.lastIndexOf('.');
        final String type = dot > file.lastIndexOf('/') ? android.webkit.MimeTypeMap.getSingleton()
            .getMimeTypeFromExtension(file.substring(dot + 1).toLowerCase(java.util.Locale.ROOT)) : null;
        android.media.MediaScannerConnection.scanFile(activity, new String[] {file}, type != null ? new String[] {type} : null, (scanned, uri) ->
            activity.runOnUiThread(() -> {
                if (uri == null) {
                    Toast.makeText(activity, "Lumen cannot open this file", Toast.LENGTH_SHORT).show();
                    return;
                }
                Intent view = new Intent(Intent.ACTION_VIEW);
                if (type != null) view.setDataAndType(uri, type); else view.setData(uri);
                view.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    activity.startActivity(Intent.createChooser(view, null));
                } catch (RuntimeException exception) {
                    Toast.makeText(activity, "No app can open this file", Toast.LENGTH_SHORT).show();
                }
            }));
    }

    /* The Downloads viewer's PDF pages: page (from 0) of the PDF at path, rendered width pixels wide on
     * white into a JPEG in the cache. Returns { page count, image path, width, height }, or null. */
    public static String[] pdfRender(byte[] path, int page, int width) {
        final SDLActivity activity = mSingleton;
        if (activity == null || path == null) return null;
        java.io.File file = new java.io.File(new String(path, StandardCharsets.UTF_8));
        try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
             android.graphics.pdf.PdfRenderer pdf = new android.graphics.pdf.PdfRenderer(fd)) {
            int count = pdf.getPageCount();
            if (count <= 0) return null;
            int index = Math.max(0, Math.min(page, count - 1));
            try (android.graphics.pdf.PdfRenderer.Page sheet = pdf.openPage(index)) {
                int w = Math.max(64, Math.min(width, 4096));
                int h = Math.max(1, Math.round((float) w * sheet.getHeight() / Math.max(1, sheet.getWidth())));
                if (h > 8192) { w = Math.max(1, Math.round(w * 8192f / h)); h = 8192; }
                android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
                bitmap.eraseColor(android.graphics.Color.WHITE);
                sheet.render(bitmap, null, null, android.graphics.pdf.PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);
                java.io.File out = new java.io.File(activity.getCacheDir(), "viewer-pdf.jpg");
                try (java.io.FileOutputStream stream = new java.io.FileOutputStream(out)) {
                    bitmap.compress(android.graphics.Bitmap.CompressFormat.JPEG, 92, stream);
                }
                bitmap.recycle();
                return new String[] { String.valueOf(count), out.getAbsolutePath(), String.valueOf(w), String.valueOf(h) };
            }
        } catch (Exception | OutOfMemoryError exception) {
            return null;
        }
    }

    public static byte[][] getTrustRoots() {
        try {
            TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
            factory.init((KeyStore) null);
            for (TrustManager manager : factory.getTrustManagers()) {
                if (!(manager instanceof X509TrustManager)) continue;
                X509Certificate[] certificates = ((X509TrustManager) manager).getAcceptedIssuers();
                byte[][] roots = new byte[certificates.length][];
                for (int index = 0; index < certificates.length; index++) {
                    roots[index] = certificates[index].getEncoded();
                }
                return roots;
            }
        } catch (GeneralSecurityException exception) {
            Log.e("Lumen", "Unable to read Android trust roots", exception);
        }
        return new byte[0][];
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    /* Page console to logcat (tag LumenJS) for this process only: launch with
       `--es org.lumen.browser.CONSOLE 2` (0 debug .. 3 errors). Not saved. */
    private void applyConsole(Intent intent) {
        String level = intent != null ? intent.getStringExtra(CONSOLE_EXTRA) : null;
        if (level == null || !level.matches("[0-3]")) return;
        try {
            Os.setenv("LUMEN_CONSOLE", level, true);
        } catch (ErrnoException exception) {
            Log.e("Lumen", "Unable to enable the page console", exception);
        }
    }

    /* Debug builds only: a probe script (base64, `--es org.lumen.browser.PRE_JS ...`) that every page
       runs before its own scripts, as LUMEN_PRE_FILE does on the desktop; FRAME_PRE_JS is the same for
       every iframe document (LUMEN_FRAME_PRE). Not saved. */
    private void applyPreJs(Intent intent) {
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) == 0) return;
        String frame = intent != null ? intent.getStringExtra(FRAME_PRE_JS_EXTRA) : null;
        if (frame != null) {
            try {
                Os.setenv("LUMEN_FRAME_PRE", new String(android.util.Base64.decode(frame, android.util.Base64.DEFAULT), java.nio.charset.StandardCharsets.UTF_8), true);
            } catch (IllegalArgumentException | ErrnoException exception) {
                Log.e("Lumen", "Unable to set the frame probe script", exception);
            }
        }
        String code = intent != null ? intent.getStringExtra(PRE_JS_EXTRA) : null;
        if (code == null) return;
        java.io.File file = new java.io.File(getCacheDir(), "pre.js");
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
            out.write(android.util.Base64.decode(code, android.util.Base64.DEFAULT));
            Os.setenv("LUMEN_PRE_FILE", file.getPath(), true);
        } catch (IllegalArgumentException | java.io.IOException | ErrnoException exception) {
            Log.e("Lumen", "Unable to set the probe script", exception);
        }
    }

    /* Debug builds only: extra V8 flags for this process, for tuning experiments. Launch with
       `--es org.lumen.browser.V8_FLAGS "--max-semi-space-size=16"`, and the script watchdog
       with `--es org.lumen.browser.JS_TIMEOUT_MS 30000` (0 disables it). Not saved. */
    private void applyV8Flags(Intent intent) {
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) == 0) return;
        String flags = intent != null ? intent.getStringExtra(V8_FLAGS_EXTRA) : null;
        String timeout = intent != null ? intent.getStringExtra(JS_TIMEOUT_EXTRA) : null;
        try {
            if (flags != null && flags.matches("[-a-z0-9_= ]{1,200}")) Os.setenv("LUMEN_V8_FLAGS", flags, true);
            if (timeout != null && timeout.matches("[0-9]{1,7}")) Os.setenv("LUMEN_JS_TIMEOUT_MS", timeout, true);
        } catch (ErrnoException exception) {
            Log.e("Lumen", "Unable to set the V8 flags", exception);
        }
    }

    /* HTTP proxy for the native network stack, read once at startup. Launch with
       `--es org.lumen.browser.PROXY host:port` to save one ("direct" clears it);
       otherwise the system proxy of the active network applies. */
    private void applyProxy(Intent intent) {
        SharedPreferences prefs = getSharedPreferences("lumen", MODE_PRIVATE);
        String extra = intent != null ? intent.getStringExtra(PROXY_EXTRA) : null;
        if (extra != null) {
            extra = extra.trim();
            if (extra.isEmpty() || extra.equalsIgnoreCase("direct")) prefs.edit().remove("proxy").apply();
            else prefs.edit().putString("proxy", extra).apply();
        }
        String proxy = prefs.getString("proxy", null), bypass = null;
        if (proxy == null) {
            String host = System.getProperty("https.proxyHost"), port = System.getProperty("https.proxyPort");
            if (host == null || host.isEmpty()) {
                host = System.getProperty("http.proxyHost");
                port = System.getProperty("http.proxyPort");
            }
            if (host != null && !host.isEmpty()) {
                proxy = host + ":" + (port != null && !port.isEmpty() ? port : "8080");
                String hosts = System.getProperty("http.nonProxyHosts");
                if (hosts != null) bypass = hosts.replace('|', ',');
            }
        }
        try {
            if (proxy != null) {
                Os.setenv("LUMEN_PROXY", proxy, true);
                if (bypass != null) Os.setenv("NO_PROXY", bypass, true);
                Log.i("Lumen", "Using an HTTP proxy");
            }
        } catch (ErrnoException exception) {
            Log.e("Lumen", "Unable to set the proxy", exception);
        }
    }

    /* Debug builds only: the device test pages, one tab each, from `--es org.lumen.browser.TEST_URLS
       "URL URL ..."` (tools/device_tests.py). "bm:TEXT" is the newest bookmark matching TEXT. */
    private String[] testUrls(Intent intent) {
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) == 0) return null;
        String extra = intent != null ? intent.getStringExtra(TEST_URLS_EXTRA) : null;
        if (extra == null) return null;
        java.util.ArrayList<String> urls = new java.util.ArrayList<>();
        for (String u : extra.trim().split("\\s+"))
            if (urls.size() < 16 && u.matches("(https?://|lumen://|bm:)\\S{1,2000}")) urls.add(u);
        if (urls.isEmpty()) return null;
        try {
            Os.setenv("LUMEN_NO_SESSION", "1", true);   /* a test run neither restores nor saves the workspaces */
        } catch (ErrnoException exception) {
            Log.e("Lumen", "Unable to mark the test run", exception);
        }
        return urls.toArray(new String[0]);
    }

    @Override
    protected synchronized String[] getArguments() {
        AndroidFontRaster.initialize();
        nativeArgumentsRequested = true;
        applyProxy(getIntent());
        applyConsole(getIntent());
        applyV8Flags(getIntent());
        applyPreJs(getIntent());
        String[] tests = testUrls(getIntent());
        if (tests != null) return tests;
        String url = openShared(getIntent()) ? null : launchUrl(getIntent());   /* a shared file opens in a tab of its own once ready */
        return new String[] {url != null ? url : "lumen://newtab"};
    }
}
