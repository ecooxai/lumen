package org.lumen.browser;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import android.util.Log;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
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
