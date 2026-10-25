package org.lumen.browser;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;

/* Read-only access to the images Lumen copies or shares (cache/clip), for the clipboard and the share sheet. */
public final class ClipProvider extends ContentProvider {
    static Uri uri(Context context, File file) {
        return new Uri.Builder().scheme("content").authority(context.getPackageName() + ".clip").appendPath(file.getName()).build();
    }

    private File file(Uri uri) {
        String name = uri.getLastPathSegment();
        if (getContext() == null || name == null || name.isEmpty() || name.startsWith(".") || name.contains("/")) return null;
        File file = new File(new File(getContext().getCacheDir(), "clip"), name);
        return file.isFile() ? file : null;
    }

    @Override public boolean onCreate() { return true; }

    @Override public String getType(Uri uri) { return file(uri) != null ? "image/png" : null; }

    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new SecurityException("read only");
        File file = file(uri);
        if (file == null) throw new FileNotFoundException(uri.toString());
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        File file = file(uri);
        if (file == null) return null;
        MatrixCursor cursor = new MatrixCursor(new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE });
        cursor.addRow(new Object[] { file.getName(), file.length() });
        return cursor;
    }

    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] args) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
}
