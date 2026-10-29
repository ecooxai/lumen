package org.lumen.browser;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.fonts.Font;
import android.graphics.fonts.FontVariationAxis;
import android.os.Build;
import java.io.File;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

public final class AndroidFontRaster {
    private AndroidFontRaster() {}

    private static native boolean nativeInitialize();

    public static boolean initialize() {
        return Build.VERSION.SDK_INT >= 31 && nativeInitialize();
    }

    private static int[] raster(String path, int collectionIndex, int[] tags, float[] values, int glyphId, float pixels) {
        if (Build.VERSION.SDK_INT < 31) return null;
        return Api31.raster(path, collectionIndex, tags, values, glyphId, pixels);
    }

    @android.annotation.TargetApi(31)
    private static final class Api31 {
        private static final Map<String, Font> fonts = new LinkedHashMap<String, Font>(16, 0.75f, true) {
            @Override
            protected boolean removeEldestEntry(Map.Entry<String, Font> eldest) {
                return size() > 16;
            }
        };

        static synchronized int[] raster(String path, int collectionIndex, int[] tags, float[] values, int glyphId, float pixels) {
            if (path == null || !path.startsWith("/") || collectionIndex < 0 || tags == null || values == null
                    || tags.length != values.length || tags.length > 16 || glyphId <= 0 || glyphId > 65535
                    || !Float.isFinite(pixels) || pixels <= 0 || pixels > 2048) return null;
            Bitmap bitmap = null;
            try {
                StringBuilder key = new StringBuilder(path).append('#').append(collectionIndex);
                FontVariationAxis[] axes = new FontVariationAxis[tags.length];
                for (int axis = 0; axis < tags.length; axis++) {
                    if (!Float.isFinite(values[axis])) return null;
                    for (int previous = 0; previous < axis; previous++) {
                        if (tags[previous] == tags[axis]) return null;
                    }
                    char[] tag = new char[4];
                    for (int index = 0; index < 4; index++) {
                        tag[index] = (char)((tags[axis] >>> (24 - index * 8)) & 255);
                        if (tag[index] < 32 || tag[index] > 126) return null;
                    }
                    axes[axis] = new FontVariationAxis(new String(tag), values[axis]);
                    key.append('/').append(tags[axis]).append(':').append(Float.floatToIntBits(values[axis]));
                }
                String identity = key.toString();
                Font font = fonts.get(identity);
                if (font == null) {
                    font = new Font.Builder(new File(path)).setTtcIndex(collectionIndex).setFontVariationSettings(axes).build();
                    fonts.put(identity, font);
                }
                Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
                paint.setTextSize(pixels);
                RectF bounds = new RectF();
                font.getGlyphBounds(glyphId, paint, bounds);
                if (!Float.isFinite(bounds.left) || !Float.isFinite(bounds.top)
                        || !Float.isFinite(bounds.right) || !Float.isFinite(bounds.bottom)
                        || bounds.isEmpty() || bounds.left < -4095 || bounds.top < -4095
                        || bounds.right > 4095 || bounds.bottom > 4095) return null;
                int left = (int)Math.floor(bounds.left) - 1;
                int top = (int)Math.floor(bounds.top) - 1;
                int width = (int)Math.ceil(bounds.right) + 1 - left;
                int height = (int)Math.ceil(bounds.bottom) + 1 - top;
                if (width <= 0 || height <= 0 || width > 2048 || height > 2048
                        || (long)width * height > 1048576) return null;
                bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
                Canvas canvas = new Canvas(bitmap);
                canvas.drawGlyphs(new int[] {glyphId}, 0, new float[] {-left, -top}, 0, 1, font, paint);
                int[] result = new int[4 + width * height];
                result[0] = width;
                result[1] = height;
                result[2] = left;
                result[3] = -top;
                bitmap.getPixels(result, 4, width, 0, 0, width, height);
                return result;
            } catch (IOException | IllegalArgumentException | OutOfMemoryError exception) {
                return null;
            } finally {
                if (bitmap != null) bitmap.recycle();
            }
        }
    }
}
