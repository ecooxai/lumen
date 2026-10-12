package org.lumen.codex.fonttest;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Typeface;
import android.os.Bundle;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import org.lumen.browser.AndroidFontRaster;

public final class FontTestActivity extends Activity {
    static { System.loadLibrary("codex_android_font_smoke"); }
    private static synchronized native int runTests(String reportPath);
    private static synchronized native int[] renderPreview();

    private Bitmap savePreview() throws IOException {
        int[] pixels = renderPreview();
        if (pixels == null || pixels.length < 3 || pixels[0] <= 0 || pixels[1] <= 0
                || (long)pixels[0] * pixels[1] != pixels.length - 2) {
            throw new IOException("Invalid engine preview");
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, 2, pixels[0], pixels[0], pixels[1], Bitmap.Config.ARGB_8888);
        try (FileOutputStream output = new FileOutputStream(new File(getFilesDir(), "CodexLumenAndroidFontPreview.png"))) {
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) throw new IOException("Cannot save engine preview");
        } catch (IOException exception) {
            bitmap.recycle();
            throw exception;
        }
        return bitmap;
    }

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView result = new TextView(this);
        result.setTypeface(Typeface.DEFAULT);
        AndroidFontRaster.initialize();
        result.setText("Running Lumen engine font tests…");
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(16, 24, 16, 24);
        content.addView(result);
        ImageView preview = new ImageView(this);
        preview.setAdjustViewBounds(true);
        preview.setContentDescription("Actual Lumen engine glyph pixels at multiple sizes, with script and emoji sequence samples");
        content.addView(preview, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        ScrollView scroll = new ScrollView(this);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        scroll.addView(content);
        setContentView(scroll);
        scroll.requestApplyInsets();
        new Thread(() -> {
            int status = runTests(new File(getFilesDir(), "font-report.txt").getAbsolutePath());
            try {
                Bitmap bitmap = savePreview();
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) { bitmap.recycle(); return; }
                    result.setText("Lumen font test exit: " + status + " / engine preview saved");
                    preview.setImageBitmap(bitmap);
                });
            } catch (IOException | RuntimeException | OutOfMemoryError exception) {
                runOnUiThread(() -> result.setText("Lumen font test exit: " + status + " / preview FAILED: " + exception.getClass().getSimpleName()));
            }
        }, "codex-font-test").start();
    }
}
