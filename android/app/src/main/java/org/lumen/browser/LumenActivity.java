package org.lumen.browser;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import android.util.Log;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.widget.Toast;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.X509Certificate;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

public final class LumenActivity extends SDLActivity {
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

    @Override
    protected synchronized String[] getArguments() {
        AndroidFontRaster.initialize();
        nativeArgumentsRequested = true;
        String url = launchUrl(getIntent());
        return new String[] {url != null ? url : "lumen://newtab"};
    }
}
