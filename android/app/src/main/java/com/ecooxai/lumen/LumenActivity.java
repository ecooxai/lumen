package com.ecooxai.lumen;

import android.Manifest;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.Toast;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import org.libsdl.app.SDLActivity;

/* Thin host for the native Lumen engine: SDL owns the surface/input, liblumen.so is the browser.
   Methods here are called from src/platform/android/android.c over JNI. */
public class LumenActivity extends SDLActivity {
    private static final String TAG = "Lumen";
    private volatile Location mLastLocation;
    private LocationListener mLocListener;

    @Override
    protected String[] getLibraries() {
        return new String[] { "wgpu_native", "lumen" };
    }

    @Override
    protected String[] getArguments() {
        Intent it = getIntent();
        Uri u = it != null ? it.getData() : null;
        return u != null ? new String[] { u.toString() } : new String[0];
    }

    /* Copies a finished download into the shared Downloads collection so it shows up in the Files app. */
    public void publishDownload(String path, String name, String mime) {
        try {
            File src = new File(path);
            if (mime == null || mime.isEmpty()) mime = "application/octet-stream";
            if (Build.VERSION.SDK_INT >= 29) {
                ContentValues v = new ContentValues();
                v.put(MediaStore.Downloads.DISPLAY_NAME, name);
                v.put(MediaStore.Downloads.MIME_TYPE, mime);
                v.put(MediaStore.Downloads.IS_PENDING, 1);
                Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                if (uri == null) return;
                try (InputStream in = new FileInputStream(src); OutputStream out = getContentResolver().openOutputStream(uri)) { copy(in, out); }
                v.clear(); v.put(MediaStore.Downloads.IS_PENDING, 0);
                getContentResolver().update(uri, v, null, null);
            } else {
                File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
                dir.mkdirs();
                try (InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(new File(dir, name))) { copy(in, out); }
            }
            runOnUiThread(() -> Toast.makeText(this, "Downloaded " + name, Toast.LENGTH_SHORT).show());
        } catch (Exception e) {
            Log.w(TAG, "publishDownload failed", e);
        }
    }

    private static void copy(InputStream in, OutputStream out) throws java.io.IOException {
        byte[] b = new byte[65536]; int n;
        while ((n = in.read(b)) > 0) out.write(b, 0, n);
    }

    /* Geolocation: returns {lat, lon, accuracy, altitude, heading, speed, timeMs} or null while no fix is known. */
    public double[] getLocation(boolean high) {
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED
            && checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) != PackageManager.PERMISSION_GRANTED) return null;
        LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        if (lm == null) return null;
        if (mLocListener == null) {
            mLocListener = new LocationListener() {
                @Override public void onLocationChanged(Location l) { mLastLocation = l; }
                @Override public void onProviderEnabled(String p) {}
                @Override public void onProviderDisabled(String p) {}
                @Override public void onStatusChanged(String p, int s, Bundle b) {}
            };
            runOnUiThread(() -> {
                try {
                    for (String p : lm.getProviders(true))
                        if (!LocationManager.PASSIVE_PROVIDER.equals(p)) lm.requestLocationUpdates(p, 1000, 0, mLocListener, Looper.getMainLooper());
                } catch (SecurityException e) { Log.w(TAG, "location", e); }
            });
        }
        Location best = mLastLocation;
        try {
            for (String p : lm.getProviders(true)) {
                Location l = lm.getLastKnownLocation(p);
                if (l != null && (best == null || l.getTime() > best.getTime())) best = l;
            }
        } catch (SecurityException e) { return null; }
        if (best == null) return null;
        return new double[] { best.getLatitude(), best.getLongitude(), best.hasAccuracy() ? best.getAccuracy() : 100,
            best.hasAltitude() ? best.getAltitude() : Double.NaN, best.hasBearing() ? best.getBearing() : Double.NaN,
            best.hasSpeed() ? best.getSpeed() : Double.NaN, best.getTime() };
    }

    public boolean hasPermission(String p) {
        return checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED;
    }

    /* Copies a picked content:// document into the cache so native code can read it by path; keeps its display name. */
    public String importContent(String uri) {
        try {
            Uri u = Uri.parse(uri);
            String name = null;
            try (android.database.Cursor c = getContentResolver().query(u, new String[] { android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) {
                if (c != null && c.moveToFirst()) name = c.getString(0);
            }
            if (name == null || name.isEmpty()) name = u.getLastPathSegment();
            if (name == null || name.isEmpty()) name = "file";
            name = name.replace('/', '_');
            File dir = new File(getCacheDir(), "picked/" + System.nanoTime());
            dir.mkdirs();
            File out = new File(dir, name);
            try (InputStream in = getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(out)) { copy(in, o); }
            return out.getAbsolutePath();
        } catch (Exception e) {
            Log.w(TAG, "importContent failed", e);
            return null;
        }
    }

    public void stopLocation() {
        LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        if (lm != null && mLocListener != null) { lm.removeUpdates(mLocListener); mLocListener = null; }
    }
}
