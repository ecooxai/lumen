package org.lumen.browser;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

/* Pages keep playing audio and video while Lumen is in the background: while the activity is paused and a page
   plays media, a media-playback foreground service keeps the process from being frozen. It stops once the media
   stops or the activity comes back. */
final class MediaPlayback {
    private static final String CHANNEL = "lumen-media";
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static boolean running, paused;
    private static Context app;

    private MediaPlayback() {}

    static void activityPaused(Context context, boolean isPaused) {
        app = context.getApplicationContext();
        paused = isPaused;
        main.removeCallbacks(check);
        if (paused) main.postDelayed(check, 500); else stop();
    }

    private static final Runnable check = new Runnable() {
        @Override public void run() {
            if (!paused) return;
            boolean playing = LumenActivity.nativeMediaPlaying();
            if (playing && !running) {
                try {
                    Intent intent = new Intent(app, Service.class);
                    if (Build.VERSION.SDK_INT >= 26) app.startForegroundService(intent); else app.startService(intent);
                    running = true;
                } catch (RuntimeException e) {
                    android.util.Log.w("Lumen", "media service: " + e);
                }
            } else if (!playing && running) stop();
            main.postDelayed(this, 3000);
        }
    };

    private static void stop() {
        if (!running || app == null) return;
        running = false;
        app.stopService(new Intent(app, Service.class));
    }

    public static final class Service extends android.app.Service {
        private PowerManager.WakeLock wake;

        @Override public IBinder onBind(Intent intent) { return null; }

        @Override public int onStartCommand(Intent intent, int flags, int startId) {
            NotificationManager notifications = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            Notification.Builder builder;
            if (Build.VERSION.SDK_INT >= 26) {
                notifications.createNotificationChannel(new NotificationChannel(CHANNEL, "Playing media", NotificationManager.IMPORTANCE_LOW));
                builder = new Notification.Builder(this, CHANNEL);
            } else builder = new Notification.Builder(this);
            Intent open = new Intent(this, LumenActivity.class).setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
            builder.setSmallIcon(android.R.drawable.ic_media_play).setContentTitle("Lumen is playing media").setOngoing(true)
                .setContentIntent(PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE));
            if (Build.VERSION.SDK_INT >= 29) startForeground(2, builder.build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            else startForeground(2, builder.build());
            if (wake == null) {
                wake = ((PowerManager) getSystemService(Context.POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "lumen:media");
                wake.acquire(4 * 60 * 60 * 1000L);
            }
            return START_NOT_STICKY;
        }

        @Override public void onDestroy() {
            if (wake != null && wake.isHeld()) wake.release();
            wake = null;
            if (Build.VERSION.SDK_INT >= 24) stopForeground(STOP_FOREGROUND_REMOVE);
            super.onDestroy();
        }
    }
}
