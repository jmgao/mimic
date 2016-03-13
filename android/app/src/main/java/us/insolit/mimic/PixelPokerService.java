package us.insolit.mimic;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.os.IBinder;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.WindowManager.LayoutParams;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

// HACK: It seems like there's a buffer in MediaCodec that causes the last few frames to get stuck
//       if nothing else happens afterwards. Superimpose a GLSurfaceView over everything else to
//       force a frame through the codec every vsync.

public class PixelPokerService extends Service {
    class PixelRenderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        }

        @Override
        public void onSurfaceChanged(GL10 gl10, int i, int i1) {
        }

        @Override
        public void onDrawFrame(GL10 gl10) {
        }
    }

    class PixelView extends GLSurfaceView {
        private final PixelRenderer mRenderer = new PixelRenderer();

        public PixelView(Context context){
            super(context);

            setEGLContextClientVersion(2);
            setRenderer(mRenderer);
            setRenderMode(RENDERMODE_CONTINUOUSLY);
        }
    }

    private WindowManager windowManager;
    private PixelView pixelView;

    public PixelPokerService() {
    }

    @Override public void onCreate() {
        super.onCreate();

        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);

        pixelView = new PixelView(this);

        LayoutParams params = new LayoutParams(
                10,
                10,
                LayoutParams.TYPE_PHONE,
                LayoutParams.FLAG_NOT_FOCUSABLE |
                        LayoutParams.FLAG_LAYOUT_NO_LIMITS | LayoutParams.FLAG_LAYOUT_IN_SCREEN |
                        LayoutParams.FLAG_DISMISS_KEYGUARD | LayoutParams.FLAG_KEEP_SCREEN_ON,
                PixelFormat.TRANSLUCENT);

        params.gravity = Gravity.BOTTOM | Gravity.RIGHT;
        params.x = 0;
        params.y = 0;

        ViewGroup.LayoutParams viewParams = new ViewGroup.LayoutParams(10, 10);
        pixelView.setLayoutParams(viewParams);

        windowManager.addView(pixelView, params);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (pixelView != null) windowManager.removeView(pixelView);
    }

    @Override
    public IBinder onBind(Intent intent) {
        // TODO: Return the communication channel to the service.
        throw new UnsupportedOperationException("Not yet implemented");
    }
}
