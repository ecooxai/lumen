package org.lumen.browser;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;
import android.webkit.MimeTypeMap;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/* Files other apps open with Lumen ("Open with") or share to it, and links they share. HTML and
   other pages open in a tab, with JavaScript; pictures, video, audio, PDF and 3D models open in
   the Downloads viewer. A file Lumen can read in place opens there, so a page's relative links
   work; anything else (most content: URIs) is copied to files/opened first. */
final class SharedFiles {
    interface Opener { boolean open(String path, boolean viewer); }
    interface Link { void open(String url); }

    private static final Pattern VIEWER = Pattern.compile(
        "(jpe?g|png|gif|webp|bmp|avif|svg|ico|mp4|m4v|webm|mkv|mov|3gp|ts|avi|ogv|mp3|m4a|aac|ogg|oga|opus|wav|flac|weba|amr|pdf|stl|obj|gltf|glb)");
    private static final Pattern PAGE = Pattern.compile("(html?|xhtml|xht|shtml|mht|svgz?|txt|md|json|xml|css|js|mjs|csv|log)");
    private static final Pattern LINK = Pattern.compile("https?://[^\\s<>\"]+", Pattern.CASE_INSENSITIVE);
    private static final long KEEP_MS = 24L * 3600 * 1000;
    private static final int MAX_FILES = 8;
    private static int serial;

    private SharedFiles() {}

    /* true when the intent was a file or a share, handled here (in the background) */
    static boolean handle(Activity activity, Intent intent, Opener opener, Link link) {
        if (intent == null) return false;
        String action = intent.getAction();
        ArrayList<Uri> uris = new ArrayList<>();
        if (Intent.ACTION_VIEW.equals(action)) {
            Uri data = intent.getData();
            if (data == null || !("content".equals(data.getScheme()) || "file".equals(data.getScheme()))) return false;
            uris.add(data);
        } else if (Intent.ACTION_SEND.equals(action)) {
            Uri stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (stream != null) uris.add(stream);
            else {
                CharSequence text = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
                Matcher found = text != null ? LINK.matcher(text) : null;
                String url = found != null && found.find() ? LaunchUrl.sanitize(found.group()) : null;
                if (url == null) { toast(activity, "Lumen can only open shared links and files"); return true; }
                link.open(url);
                return true;
            }
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            ArrayList<Uri> list = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (list != null) for (Uri uri : list) if (uri != null && uris.size() < MAX_FILES) uris.add(uri);
        } else return false;
        if (uris.isEmpty()) return true;
        final Activity owner = activity;
        new Thread(() -> {
            clean(new File(owner.getFilesDir(), "opened"));
            for (Uri uri : uris) {
                String failure;
                try {
                    String path = local(owner, uri);
                    if (path == null) failure = "Lumen cannot open this kind of file";
                    else {
                        String extension = extension(new File(path).getName());
                        boolean viewer = VIEWER.matcher(extension).matches();
                        failure = opener.open(path, viewer) ? null : "Unable to open the file: browser is busy";
                    }
                } catch (IOException | RuntimeException exception) {
                    Log.w("Lumen", "Unable to open a shared file: " + exception.getClass().getSimpleName());
                    failure = "Lumen cannot read this file";
                }
                if (failure != null) toast(owner, failure);
            }
        }, "LumenOpenFile").start();
        return true;
    }

    /* a path Lumen can read for uri, copying it in when needed; null for kinds Lumen does not show */
    private static String local(Activity activity, Uri uri) throws IOException {
        long[] size = {-1};
        String name = nameOf(activity, uri, size);
        if (name == null) return null;
        String extension = extension(name);
        if (!VIEWER.matcher(extension).matches() && !PAGE.matcher(extension).matches()) return null;
        return readable(activity, uri, name, size[0], new File(activity.getFilesDir(), "opened"));
    }

    /* the files the system's picker chose for an <input type=file>, one path per line, or null:
       files in shared storage in place, anything else copied to cache/picked (kept a day) */
    static String picked(Activity activity, Intent data) {
        ArrayList<Uri> uris = new ArrayList<>();
        if (data != null && data.getClipData() != null)
            for (int i = 0; i < data.getClipData().getItemCount() && uris.size() < 64; i++) uris.add(data.getClipData().getItemAt(i).getUri());
        else if (data != null && data.getData() != null) uris.add(data.getData());
        File picked = new File(activity.getCacheDir(), "picked");
        clean(picked);
        StringBuilder paths = new StringBuilder();
        for (Uri uri : uris) {
            try {
                long[] size = {-1};
                String name = nameOf(activity, uri, size), path = name != null ? readable(activity, uri, name, size[0], picked) : null;
                if (path == null || path.indexOf('\n') >= 0) continue;
                if (paths.length() > 0) paths.append('\n');
                paths.append(path);
            } catch (IOException | RuntimeException exception) {
                Log.w("Lumen", "Unable to read a picked file: " + exception.getClass().getSimpleName());
            }
        }
        return paths.length() > 0 ? paths.toString() : null;
    }

    /* uri's file name (with an extension from its type when it has none) and size; null for Lumen's own data */
    private static String nameOf(Activity activity, Uri uri, long[] size) throws IOException {
        ContentResolver resolver = activity.getContentResolver();
        String name = null;
        if ("file".equals(uri.getScheme())) {   /* never Lumen's own data */
            File file = new File(uri.getPath() != null ? uri.getPath() : "").getCanonicalFile();
            if (file.getPath().startsWith(new File(activity.getApplicationInfo().dataDir).getCanonicalPath() + "/")) return null;
            name = file.getName();
        }
        else try (Cursor cursor = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                if (!cursor.isNull(0)) name = cursor.getString(0);
                if (!cursor.isNull(1)) size[0] = cursor.getLong(1);
            }
        } catch (RuntimeException ignored) {}
        if (name == null || name.isEmpty()) name = uri.getLastPathSegment() != null ? new File(uri.getLastPathSegment()).getName() : "file";
        name = safeName(name);
        String type = resolver.getType(uri);
        if (extension(name).isEmpty() && type != null) {
            String guessed = MimeTypeMap.getSingleton().getExtensionFromMimeType(type);
            if (guessed != null) name += "." + guessed;
        }
        return name;
    }

    /* uri's file in place when Lumen can read it there, else a copy in a new folder under into */
    private static String readable(Activity activity, Uri uri, String name, long size, File into) throws IOException {
        ContentResolver resolver = activity.getContentResolver();
        File direct = inPlace(activity, uri, name, size);
        if (direct != null) return direct.getAbsolutePath();
        File folder = new File(into, System.currentTimeMillis() + "-" + (serial++));
        if (!folder.mkdirs()) throw new IOException("mkdirs");
        File out = new File(folder, name);
        try (InputStream in = resolver.openInputStream(uri); OutputStream copy = new FileOutputStream(out)) {
            if (in == null) throw new IOException("no stream");
            byte[] buffer = new byte[1 << 16];
            for (int n; (n = in.read(buffer)) > 0; ) copy.write(buffer, 0, n);
        }
        return out.getAbsolutePath();
    }

    /* the file itself when uri names a readable one in shared storage, e.g. a file manager's
       content://…/root/storage/emulated/0/Download/page.html, so its folder's other files load too */
    private static File inPlace(Activity activity, Uri uri, String name, long size) {
        String path = uri.getPath();
        if (path == null) return null;
        if (!"file".equals(uri.getScheme())) {
            int at = path.indexOf("/storage/");
            if (at < 0) at = path.indexOf("/sdcard/");
            if (at < 0) return null;
            path = path.substring(at);
        }
        try {
            File file = new File(path).getCanonicalFile();
            String data = new File(activity.getApplicationInfo().dataDir).getCanonicalPath();
            if (file.getPath().startsWith(data + "/") || !file.isFile() || !file.canRead()) return null;
            if (!"file".equals(uri.getScheme()) && (!safeName(file.getName()).equals(name) || (size >= 0 && file.length() != size))) return null;
            return file;
        } catch (IOException | SecurityException exception) {
            return null;
        }
    }

    private static String safeName(String name) {
        String clean = name.replaceAll("[\\x00-\\x1f/\\\\]", "_").trim();
        while (clean.startsWith(".")) clean = clean.substring(1);
        if (clean.length() > 120) {
            String extension = extension(clean);
            clean = clean.substring(0, 100) + (extension.isEmpty() ? "" : "." + extension);
        }
        return clean.isEmpty() ? "file" : clean;
    }

    private static String extension(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 && dot < name.length() - 1 ? name.substring(dot + 1).toLowerCase(Locale.ROOT) : "";
    }

    /* copies from earlier days */
    private static void clean(File opened) {
        File[] folders = opened.listFiles();
        if (folders == null) return;
        long now = System.currentTimeMillis();
        for (File folder : folders) {
            if (now - folder.lastModified() < KEEP_MS) continue;
            File[] files = folder.listFiles();
            if (files != null) for (File file : files) file.delete();
            folder.delete();
        }
    }

    private static void toast(Activity activity, String text) {
        activity.runOnUiThread(() -> Toast.makeText(activity, text, Toast.LENGTH_SHORT).show());
    }
}
