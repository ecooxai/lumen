package org.lumen.codex.fonttest;

import android.app.Activity;
import android.graphics.Typeface;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;

public final class FontTestActivity extends Activity {
    static { System.loadLibrary("codex_android_font_smoke"); }
    private static synchronized native int runTests(String reportPath);

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView result = new TextView(this);
        result.setTypeface(Typeface.DEFAULT);
        result.setText("Running Lumen engine font tests…");
        setContentView(result);
        new Thread(() -> {
            int status = runTests(new File(getFilesDir(), "font-report.txt").getAbsolutePath());
            runOnUiThread(() -> result.setText("Lumen font test exit: " + status));
        }, "codex-font-test").start();
    }
}
