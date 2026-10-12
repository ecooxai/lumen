package org.lumen.browser;

import org.libsdl.app.SDLActivity;
import android.util.Log;
import android.content.Intent;
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
        nativeArgumentsRequested = true;
        String url = launchUrl(getIntent());
        return new String[] {url != null ? url : "lumen://newtab"};
    }
}
