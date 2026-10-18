package org.lumen.browser;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import android.util.Log;
import android.content.Context;
import android.content.Intent;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.SharedPreferences;
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
    private static final String PROXY_EXTRA = "org.lumen.browser.PROXY";
    private boolean nativeArgumentsRequested;
    private static native boolean nativeOpenUrl(byte[] url);
    private static native boolean nativeRequestBack();
    private static native void nativeUiAction(int code, int value, byte[] first, byte[] second);
    private Runnable unregisterBack;
    private BrowserSurface browserSurface;

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
        if (!nativeArgumentsRequested || mBrokenLibraries) {
            moveTaskToBack(true);
        } else if (!nativeRequestBack()) {
            Toast.makeText(this, "Unable to go back: browser is busy", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!mBrokenLibraries && mLayout != null) BrowserInsets.install(getWindow(), mLayout);
        if (Build.VERSION.SDK_INT >= 33) unregisterBack = Api33.registerBack(this);
    }

    @Override
    public void onBackPressed() {
        requestBack();
    }

    @Override
    protected void onDestroy() {
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
            url = launchUrl(intent);
            if (!nativeArgumentsRequested || mBrokenLibraries || url == null) return;
        }
        if (!nativeOpenUrl(url.getBytes(StandardCharsets.UTF_8))) {
            Toast.makeText(this, "Unable to open link: browser is busy", Toast.LENGTH_SHORT).show();
        }
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
        if (activity == null || state == null || state.length < 7) return;
        final int[] copy = state.clone();
        activity.runOnUiThread(() -> BrowserMenus.showMenu(activity, copy, UI_ACTION));
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

    @Override
    protected synchronized String[] getArguments() {
        AndroidFontRaster.initialize();
        nativeArgumentsRequested = true;
        applyProxy(getIntent());
        String url = launchUrl(getIntent());
        return new String[] {url != null ? url : "lumen://newtab"};
    }
}
