package nl.toxicradium.buddy;

import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import com.getcapacitor.BridgeActivity;

/*
 * Buddy blijft doorwerken als je naar een andere app gaat.
 * Android "bevriest" anders de webpagina van de app zodra hij op de achtergrond staat:
 * dan stopt alles (GPS-verwerking, Bluetooth naar Buddy+, navigatie).
 * 1. De pagina krijgt dezelfde voorrang als wanneer hij in beeld is (niet laten bevriezen).
 * 2. De pagina blijft denken dat hij zichtbaar is, zodat de klokjes gewoon doorlopen.
 * 3. In de autostand gaan de Android-balken (klok bovenin, knoppen onderin) weg: volledig scherm.
 *    Even vegen vanaf de rand laat ze tijdelijk terugkomen.
 */
public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            WebView w = getBridge().getWebView();
            if (Build.VERSION.SDK_INT >= 26) w.setRendererPriorityPolicy(WebView.RENDERER_PRIORITY_IMPORTANT, false);
            w.addJavascriptInterface(new Balken(), "BuddyBalken");
        } catch (Exception e) { }
    }

    private volatile boolean balkenWeg = false;

    public class Balken {
        @JavascriptInterface
        public void zet(final boolean zien) {
            balkenWeg = !zien;
            runOnUiThread(new Runnable() { public void run() { pasBalkenToe(); } });
        }
    }

    private void pasBalkenToe() {
        try {
            WindowInsetsControllerCompat c = WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
            if (balkenWeg) {
                c.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                c.hide(WindowInsetsCompat.Type.systemBars());
            } else {
                c.show(WindowInsetsCompat.Type.systemBars());
            }
        } catch (Exception e) { }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && balkenWeg) pasBalkenToe();
    }

    @Override
    public void onPause() {
        super.onPause();
        houdWakker();
    }

    @Override
    public void onStop() {
        super.onStop();
        houdWakker();
    }

    private void houdWakker() {
        try {
            WebView w = getBridge().getWebView();
            w.onResume();
            w.resumeTimers();
            w.dispatchWindowVisibilityChanged(View.VISIBLE);
        } catch (Exception e) { }
    }
}
