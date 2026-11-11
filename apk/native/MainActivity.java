package nl.toxicradium.buddy;

import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.webkit.WebView;
import com.getcapacitor.BridgeActivity;

/*
 * Buddy blijft doorwerken als je naar een andere app gaat.
 * Android "bevriest" anders de webpagina van de app zodra hij op de achtergrond staat:
 * dan stopt alles (GPS-verwerking, Bluetooth naar Buddy+, navigatie).
 * 1. De pagina krijgt dezelfde voorrang als wanneer hij in beeld is (niet laten bevriezen).
 * 2. De pagina blijft denken dat hij zichtbaar is, zodat de klokjes gewoon doorlopen.
 */
public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            WebView w = getBridge().getWebView();
            if (Build.VERSION.SDK_INT >= 26) w.setRendererPriorityPolicy(WebView.RENDERER_PRIORITY_IMPORTANT, false);
        } catch (Exception e) { }
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
