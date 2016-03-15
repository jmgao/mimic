package us.insolit.mimic;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.WindowManager.LayoutParams;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

// HACK: It seems like there's a buffer in MediaCodec that causes the last few frames to get stuck
//       if nothing else happens afterwards. Superimpose a GLSurfaceView over everything else to
//       force a frame through the codec every vsync.

public class PixelPoker {
    static class PixelRenderer implements GLSurfaceView.Renderer {
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

    static class PixelView extends GLSurfaceView {
        private final PixelRenderer mRenderer = new PixelRenderer();

        public PixelView(Context context){
            super(context);

            setEGLContextClientVersion(2);
            setRenderer(mRenderer);
            setRenderMode(RENDERMODE_CONTINUOUSLY);
        }
    }

    private static WindowManager windowManager;
    private static PixelView pixelView;
    private static final int viewWidth = 10;
    private static final int viewHeight = 10;

    static void start(Context ctx) {
        windowManager = (WindowManager) ctx.getSystemService(Context.WINDOW_SERVICE);

        pixelView = new PixelView(ctx);

        LayoutParams params = new LayoutParams(
                viewWidth,
                viewHeight,
                LayoutParams.TYPE_PHONE,
                LayoutParams.FLAG_NOT_FOCUSABLE |
                        LayoutParams.FLAG_LAYOUT_NO_LIMITS | LayoutParams.FLAG_LAYOUT_IN_SCREEN |
                        LayoutParams.FLAG_DISMISS_KEYGUARD | LayoutParams.FLAG_KEEP_SCREEN_ON,
                PixelFormat.TRANSLUCENT);

        params.gravity = Gravity.BOTTOM | Gravity.RIGHT;
        params.x = 0;
        params.y = 0;

        ViewGroup.LayoutParams viewParams = new ViewGroup.LayoutParams(viewWidth, viewHeight);
        pixelView.setLayoutParams(viewParams);

        windowManager.addView(pixelView, params);
    }

    public static void stop() {
        if (pixelView != null) windowManager.removeView(pixelView);
        pixelView = null;
        windowManager = null;

    }
}
