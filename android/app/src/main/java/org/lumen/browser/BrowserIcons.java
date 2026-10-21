package org.lumen.browser;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;

/* A menu action's outline icon, the same one the native toolbar draws (android_ui_icon): a path on
 * a 24 unit grid, stroked in one color. */
final class BrowserIcons extends Drawable {
    /* site info's own outlines, in the same path text */
    static final String LOCK = "M6.5 11 L17.5 11 L17.5 20 L6.5 20 Z M8.8 11 L8.8 8.2 A 12 8.2 3.2 180 180 M15.2 8.2 L15.2 11 M12 14.3 L12 16.6",
        INFO = "O 12 12 8.5 M12 11 L12 16.2 O 12 7.9 0.35",
        WARNING = "M12 3.8 L21 19.5 L3 19.5 Z M12 9.5 L12 14 O 12 16.9 0.35",
        SEARCH = "O 10.5 10.5 6 M14.8 14.8 L19.5 19.5",
        COOKIE = "O 12 12 8.5 O 9 9 0.5 O 14.8 9.8 0.5 O 9.6 14.6 0.5 O 14.6 15 0.5 O 12 12.2 0.3",
        TRASH = "M4.5 7 L19.5 7 M9.5 7 L9.5 4.5 L14.5 4.5 L14.5 7 M6.5 7 L7.5 20 L16.5 20 L17.5 7 M10.3 10.5 L10.3 16.5 M13.7 10.5 L13.7 16.5";

    private final Path path = new Path();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final int size;

    private BrowserIcons(String source, int color, int size) {
        this.size = size;
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.8f * size / 24);
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setColor(color);
        parse(source);
        android.graphics.Matrix scale = new android.graphics.Matrix();
        scale.setScale(size / 24f, size / 24f);
        path.transform(scale);
        setBounds(0, 0, size, size);
    }

    /* null when the action has no icon (or the native library is not there) */
    static Drawable of(int code, int color, int size) {
        String source;
        try { source = LumenActivity.nativeUiIcon(code); } catch (UnsatisfiedLinkError error) { return null; }
        return source != null ? new BrowserIcons(source, color, size) : null;
    }

    /* an outline given as android_ui_icon's path text */
    static Drawable path(String source, int color, int size) { return new BrowserIcons(source, color, size); }

    private void parse(String source) {
        java.util.ArrayList<String> tokens = new java.util.ArrayList<>();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("[A-Z]|-?[0-9.]+").matcher(source);
        while (m.find()) tokens.add(m.group());
        String[] t = tokens.toArray(new String[0]);
        for (int i = 0; i < t.length; ) {
            String op = t[i++];
            switch (op) {
                case "M": path.moveTo(f(t[i]), f(t[i + 1])); i += 2; break;
                case "L": path.lineTo(f(t[i]), f(t[i + 1])); i += 2; break;
                case "Z": path.close(); break;
                case "O": path.addCircle(f(t[i]), f(t[i + 1]), f(t[i + 2]), Path.Direction.CW); i += 3; break;
                case "A": {
                    float x = f(t[i]), y = f(t[i + 1]), r = f(t[i + 2]);
                    path.arcTo(new RectF(x - r, y - r, x + r, y + r), f(t[i + 3]), f(t[i + 4]), true);
                    i += 5;
                    break;
                }
                default: return;
            }
        }
    }

    private static float f(String value) { return Float.parseFloat(value); }

    @Override public void draw(Canvas canvas) {
        canvas.save();
        canvas.translate(getBounds().left, getBounds().top);
        canvas.drawPath(path, paint);
        canvas.restore();
    }
    @Override public int getIntrinsicWidth() { return size; }
    @Override public int getIntrinsicHeight() { return size; }
    @Override public void setAlpha(int alpha) { paint.setAlpha(alpha); invalidateSelf(); }
    @Override public void setColorFilter(ColorFilter filter) { paint.setColorFilter(filter); invalidateSelf(); }
    @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
}
