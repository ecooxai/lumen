package org.lumen.browser;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioPlaybackCaptureConfiguration;
import android.media.AudioRecord;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;

/* getDisplayMedia: the screen through MediaProjection. The native side asks with start(); Android shows
 * its consent dialog, then a foreground service (required for a projection) mirrors the screen into an
 * ImageReader at most maxWidth x maxHeight. Each new image (the virtual display only produces one when
 * the screen changes) goes to the native live source, at most fps a second. With audio wanted (and the
 * record permission, which the native side asks for first), the projection's playback capture records
 * what media and games play, mono float at 48 kHz in 20 ms blocks, for the page's system audio track.
 * The service may outlive its process's engine (Android restarts a pending start after a crash): it then
 * stops without calling into native code. */
final class ScreenCapture {
    static final int REQUEST = 0x4c53;
    private static final String CHANNEL = "capture";
    /* state: 1 capturing (width, height, audio: the playback capture runs), -1 refused or failed, 0 ended */
    private static native void nativeState(int state, int width, int height, boolean audio);
    private static native void nativeFrame(java.nio.ByteBuffer pixels, int width, int height, int rowStride);
    private static native void nativeAudio(float[] pcm, int samples);

    private static int maxWidth, maxHeight, fps;
    private static boolean asking, wanted, withAudio;
    private static AudioRecord audio;
    private static Thread audioThread;
    private static volatile boolean audioRun;
    private static MediaProjection projection;
    private static VirtualDisplay display;
    private static ImageReader reader;
    private static HandlerThread thread;
    private static long lastFrame;

    /* from the native thread */
    public static void start(int width, int height, int rate, boolean withSound) {
        final SDLActivity activity = LumenActivity.current();
        if (activity == null) { refuse(); return; }
        activity.runOnUiThread(() -> {
            synchronized (ScreenCapture.class) {
                if (asking || projection != null) { refuse(); return; }
                maxWidth = width; maxHeight = height; fps = Math.max(1, Math.min(60, rate)); withAudio = withSound;
                asking = wanted = true;
            }
            try {
                MediaProjectionManager manager = (MediaProjectionManager) activity.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
                activity.startActivityForResult(manager.createScreenCaptureIntent(), REQUEST);
            } catch (RuntimeException exception) {
                Log.w("Lumen", "Unable to ask for screen capture", exception);
                synchronized (ScreenCapture.class) { asking = false; }
                refuse();
            }
        });
    }

    private static void refuse() { nativeState(-1, 0, 0, false); }

    /* from the native thread: the page stopped its track */
    public static void stop() {
        final SDLActivity activity = LumenActivity.current();
        synchronized (ScreenCapture.class) { wanted = false; }
        if (activity != null) activity.runOnUiThread(() -> end(activity, false));
    }

    /* LumenActivity.onActivityResult */
    static boolean result(Activity activity, int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST) return false;
        boolean go;
        synchronized (ScreenCapture.class) { asking = false; go = wanted; }
        if (resultCode != Activity.RESULT_OK || data == null || !go) { refuse(); return true; }
        Intent service = new Intent(activity, Service.class).putExtra("code", resultCode).putExtra("data", data);
        try {
            if (Build.VERSION.SDK_INT >= 26) activity.startForegroundService(service);
            else activity.startService(service);
        } catch (RuntimeException exception) {
            Log.w("Lumen", "Unable to start screen capture", exception);
            refuse();
        }
        return true;
    }

    /* in the foreground service, once it is in the foreground */
    private static void begin(Context context, int code, Intent data) {
        MediaProjection p;
        try {
            MediaProjectionManager manager = (MediaProjectionManager) context.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
            p = manager.getMediaProjection(code, data);
        } catch (RuntimeException exception) {
            Log.w("Lumen", "Unable to start screen capture", exception);
            p = null;
        }
        synchronized (ScreenCapture.class) {
            if (p != null && !wanted) { p.stop(); p = null; }
        }
        if (p == null) { refuse(); context.stopService(new Intent(context, Service.class)); return; }
        DisplayMetrics metrics = new DisplayMetrics();
        ((WindowManager) context.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealMetrics(metrics);
        float scale = Math.min(1f, Math.min(maxWidth / (float) metrics.widthPixels, maxHeight / (float) metrics.heightPixels));
        int width = Math.max(2, Math.round(metrics.widthPixels * scale) & ~1), height = Math.max(2, Math.round(metrics.heightPixels * scale) & ~1);
        thread = new HandlerThread("lumen-screen");
        thread.start();
        Handler handler = new Handler(thread.getLooper());
        final long interval = 1000 / fps;
        lastFrame = 0;
        reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 3);
        reader.setOnImageAvailableListener(r -> {
            Image image;
            try { image = r.acquireLatestImage(); } catch (RuntimeException exception) { return; }
            if (image == null) return;
            long now = SystemClock.uptimeMillis();
            if (now - lastFrame >= interval - 2) {
                lastFrame = now;
                Image.Plane plane = image.getPlanes()[0];
                nativeFrame(plane.getBuffer(), image.getWidth(), image.getHeight(), plane.getRowStride());
            }
            image.close();
        }, handler);
        p.registerCallback(new MediaProjection.Callback() {
            @Override public void onStop() {
                SDLActivity activity = LumenActivity.current();
                if (activity != null) activity.runOnUiThread(() -> end(activity, true));
            }
        }, handler);
        projection = p;
        try {
            display = p.createVirtualDisplay("Lumen screen", width, height, Math.max(1, Math.round(metrics.densityDpi * scale)),
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, reader.getSurface(), null, handler);
        } catch (RuntimeException exception) {
            Log.w("Lumen", "Unable to mirror the screen", exception);
            end(context, false);
            refuse();
            return;
        }
        nativeState(1, width, height, withAudio && startAudio(context, p));
    }

    /* the playback capture (Android 10+), on its own thread: false if it cannot run */
    private static boolean startAudio(Context context, MediaProjection p) {
        if (Build.VERSION.SDK_INT < 29 || context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return false;
        AudioRecord record;
        try {
            AudioPlaybackCaptureConfiguration config = new AudioPlaybackCaptureConfiguration.Builder(p)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA).addMatchingUsage(AudioAttributes.USAGE_GAME).addMatchingUsage(AudioAttributes.USAGE_UNKNOWN).build();
            AudioFormat format = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_IN_MONO).build();
            int min = AudioRecord.getMinBufferSize(48000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT);
            record = new AudioRecord.Builder().setAudioFormat(format).setBufferSizeInBytes(Math.max(min, 48000 * 4 / 4)).setAudioPlaybackCaptureConfig(config).build();
            if (record.getState() != AudioRecord.STATE_INITIALIZED) { record.release(); return false; }
            record.startRecording();
        } catch (RuntimeException exception) {
            Log.w("Lumen", "Unable to capture the system audio", exception);
            return false;
        }
        final AudioRecord r = record;
        audio = r; audioRun = true;
        audioThread = new Thread(() -> {
            float[] block = new float[960];
            while (audioRun) {
                int n = r.read(block, 0, block.length, AudioRecord.READ_BLOCKING);
                if (n > 0) nativeAudio(block, n);
                else if (n < 0) break;
            }
        }, "lumen-screen-audio");
        audioThread.start();
        return true;
    }

    private static void stopAudio() {
        AudioRecord r = audio;
        Thread t = audioThread;
        audio = null; audioThread = null; audioRun = false;
        if (r == null) return;
        try { r.stop(); } catch (RuntimeException ignored) { }
        if (t != null) try { t.join(500); } catch (InterruptedException ignored) { }
        r.release();
    }

    /* UI thread */
    private static void end(Context context, boolean tell) {
        MediaProjection p = projection;
        projection = null;
        stopAudio();
        if (display != null) { display.release(); display = null; }
        if (p != null) p.stop();
        if (reader != null) { reader.close(); reader = null; }
        if (thread != null) { thread.quitSafely(); thread = null; }
        context.stopService(new Intent(context, Service.class));
        if (tell && p != null) nativeState(0, 0, 0, false);
    }

    /* the foreground service a media projection needs while it runs */
    public static final class Service extends android.app.Service {
        @Override public IBinder onBind(Intent intent) { return null; }

        @Override public int onStartCommand(Intent intent, int flags, int startId) {
            NotificationManager notifications = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            Notification.Builder builder;
            if (Build.VERSION.SDK_INT >= 26) {
                notifications.createNotificationChannel(new NotificationChannel(CHANNEL, "Screen sharing", NotificationManager.IMPORTANCE_LOW));
                builder = new Notification.Builder(this, CHANNEL);
            } else builder = new Notification.Builder(this);
            Notification notification = builder.setSmallIcon(android.R.drawable.ic_menu_camera)
                .setContentTitle("Lumen is sharing your screen").setOngoing(true).build();
            if (Build.VERSION.SDK_INT >= 29) startForeground(1, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
            else startForeground(1, notification);
            Intent data = intent != null ? intent.getParcelableExtra("data") : null;
            /* no engine in this process (a start Android redelivered after the browser crashed): nothing to feed */
            if (data != null && LumenActivity.current() != null) begin(this, intent.getIntExtra("code", 0), data);
            else stopSelf();
            return START_NOT_STICKY;
        }

        @Override public void onDestroy() {
            if (Build.VERSION.SDK_INT >= 24) stopForeground(STOP_FOREGROUND_REMOVE);
            super.onDestroy();
        }
    }
}
