package org.lumen.browser;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;

/* The CPU readout's numbers, sampled every 3 seconds on a thread of its own so they keep coming
 * while the engine thread is busy. Apps cannot read /proc/stat, so the phone's CPU use is worked
 * out from the cores' idle time (cpuidle) against the time that passed. */
final class BrowserStats {
    interface Listener { void stats(BrowserStats stats); }

    static final int PERIOD_MS = 3000;
    private static final String CPU = "/sys/devices/system/cpu/";

    /* the phone's CPU use and Lumen's, as % of all the phone's cores (-1 unknown) */
    float system = -1, browser = -1;
    /* the current tab's CPU (% of all cores), the JS heap and V8 external memory of all tabs (MB) */
    float tab, heap, external;
    int tabs, cores;
    /* bytes: Lumen's resident memory, the phone's memory and how much of it is available */
    long resident, memoryTotal, memoryAvailable;

    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final byte[] buffer = new byte[4096];
    private HandlerThread thread;
    private Handler handler;
    private long lastWall, lastCpu;
    /* per core: its cpuidle state time files, the idle time and wall clock at the last sample (0: offline) */
    private String[][] idleFiles;
    private long[] lastIdle, lastSeen;

    BrowserStats(Listener listener) { this.listener = listener; }

    /* starts or stops the sampling; the first numbers come at once */
    void run(boolean on) {
        if (on == (thread != null)) return;
        if (!on) {
            thread.quitSafely();
            thread = null;
            handler = null;
            return;
        }
        thread = new HandlerThread("LumenStats", android.os.Process.THREAD_PRIORITY_BACKGROUND);
        thread.start();
        handler = new Handler(thread.getLooper());
        final Handler own = handler;
        handler.post(new Runnable() {
            @Override public void run() {
                sample();
                main.post(() -> { if (handler == own) listener.stats(BrowserStats.this); });
                own.postDelayed(this, PERIOD_MS);
            }
        });
    }

    private void sample() {
        long wall = SystemClock.elapsedRealtime(), cpu = android.os.Process.getElapsedCpuTime();
        sampleSystem();
        int all = Math.max(1, cores > 0 ? cores : Runtime.getRuntime().availableProcessors());
        if (lastWall != 0 && wall > lastWall) browser = 100f * (cpu - lastCpu) / (wall - lastWall) / all;
        lastWall = wall;
        lastCpu = cpu;
        String statm = read("/proc/self/statm");
        if (statm != null) {
            String[] fields = statm.trim().split("\\s+");
            if (fields.length > 1) resident = number(fields[1]) * 4096;
        }
        String meminfo = read("/proc/meminfo");
        if (meminfo != null) {
            for (String line : meminfo.split("\n")) {
                if (line.startsWith("MemTotal:")) memoryTotal = kilobytes(line);
                else if (line.startsWith("MemAvailable:")) memoryAvailable = kilobytes(line);
            }
        }
        try {
            float[] native_ = LumenActivity.nativeTabStats();
            if (native_ != null && native_.length >= 4) {
                tab = native_[0] / all;
                heap = native_[1];
                external = native_[2];
                tabs = Math.round(native_[3]);
            }
        } catch (UnsatisfiedLinkError ignored) { }
    }

    private void sampleSystem() {
        if (idleFiles == null) {
            ArrayList<String[]> all = new ArrayList<>();
            for (int core = 0; ; core++) {
                if (!new File(CPU + "cpu" + core).isDirectory()) break;
                File[] states = new File(CPU + "cpu" + core + "/cpuidle").listFiles((dir, name) -> name.startsWith("state"));
                String[] files = new String[states != null ? states.length : 0];
                for (int i = 0; i < files.length; i++) files[i] = states[i].getPath() + "/time";
                all.add(files);
            }
            idleFiles = all.toArray(new String[0][]);
            lastIdle = new long[idleFiles.length];
            lastSeen = new long[idleFiles.length];
        }
        boolean[] online = online(idleFiles.length);
        long now = SystemClock.elapsedRealtimeNanos() / 1000, busy = 0, total = 0;
        int count = 0;
        for (int core = 0; core < idleFiles.length; core++) {
            if (!online[core] || idleFiles[core].length == 0) { lastSeen[core] = 0; continue; }
            count++;
            long idle = 0;
            boolean ok = true;
            for (String file : idleFiles[core]) {
                String value = read(file);
                if (value == null) { ok = false; break; }
                idle += number(value.trim());
            }
            if (!ok) { lastSeen[core] = 0; continue; }
            if (lastSeen[core] != 0 && now > lastSeen[core]) {
                long passed = now - lastSeen[core], rested = Math.max(0, Math.min(passed, idle - lastIdle[core]));
                busy += passed - rested;
                total += passed;
            }
            lastIdle[core] = idle;
            lastSeen[core] = now;
        }
        cores = count;
        if (total > 0) system = 100f * busy / total;
    }

    /* /sys/devices/system/cpu/online, such as "0-3,5" */
    private boolean[] online(int n) {
        boolean[] on = new boolean[n];
        String list = read(CPU + "online");
        if (list == null) { java.util.Arrays.fill(on, true); return on; }
        for (String range : list.trim().split(",")) {
            if (range.isEmpty()) continue;
            String[] ends = range.split("-");
            int from = (int) number(ends[0]), to = ends.length > 1 ? (int) number(ends[1]) : from;
            for (int core = Math.max(0, from); core <= to && core < n; core++) on[core] = true;
        }
        return on;
    }

    private String read(String path) {
        try (FileInputStream in = new FileInputStream(path)) {
            int n = 0, got;
            while (n < buffer.length && (got = in.read(buffer, n, buffer.length - n)) > 0) n += got;
            return new String(buffer, 0, n, java.nio.charset.StandardCharsets.US_ASCII);
        } catch (IOException | SecurityException error) {
            return null;
        }
    }

    private static long kilobytes(String line) {
        String[] fields = line.trim().split("\\s+");
        return fields.length > 1 ? number(fields[1]) * 1024 : 0;
    }

    private static long number(String text) {
        try { return Long.parseLong(text); } catch (NumberFormatException error) { return 0; }
    }
}
