package us.insolit.mimic;

import android.app.Activity;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.hardware.display.VirtualDisplay;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.PowerManager;
import android.util.Log;
import android.view.Surface;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.WritableByteChannel;

public class StreamService extends Service {
    private static final String TAG = "StreamService";

    FileOutputStream fos;
    WritableByteChannel channel;
    MediaProjectionManager projectionManager;
    MediaProjection projection;
    MediaCodec videoEncoder;
    VirtualDisplay display;
    Surface surface;
    PowerManager.WakeLock wakeLock;

    int lastOrientation;
    BroadcastReceiver rotationReceiver;

    public StreamService() {
    }

    private int getWidth() {
        return 800;
    }

    private int getHeight() {
        return 480;
    }

    private int getDPI() {
        return 120;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        PowerManager powerManager = (PowerManager)getSystemService(Context.POWER_SERVICE);
        wakeLock = powerManager.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK | PowerManager.ACQUIRE_CAUSES_WAKEUP | PowerManager.ON_AFTER_RELEASE, "mimic");
        wakeLock.acquire();

        projectionManager = (MediaProjectionManager)getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        UsbManager usbManager = (UsbManager)getSystemService(Context.USB_SERVICE);
        UsbAccessory accessory = intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY);

        ParcelFileDescriptor pfd = usbManager.openAccessory(accessory);
        if (pfd == null) {
            throw new RuntimeException("Failed to open USB accessory");
        }
        fos = new ParcelFileDescriptor.AutoCloseOutputStream(pfd);
        channel = fos.getChannel();

        lastOrientation = getResources().getConfiguration().orientation;

        Intent projectionIntent = intent.getParcelableExtra(Intent.EXTRA_INTENT);
        projection = projectionManager.getMediaProjection(Activity.RESULT_OK, projectionIntent);
        display = projection.createVirtualDisplay("Mimic", getWidth(), getHeight(), getDPI(), 0, surface, null, null);
        constructEncoder();
        configureEncoder();

        // Register a BroadcastReceiver to monitor for rotation change.
        // TODO: Send this information to the other side.
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_CONFIGURATION_CHANGED);
        rotationReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                int orientation = getResources().getConfiguration().orientation;
                if (orientation != lastOrientation) {
                    lastOrientation = orientation;
                    if (lastOrientation == Configuration.ORIENTATION_LANDSCAPE) {
                        Log.w(TAG, "New orientation: landscape");
                    } else if (lastOrientation == Configuration.ORIENTATION_PORTRAIT) {
                        Log.w(TAG, "New orientation: portrait");
                    } else {
                        Log.e(TAG, "New orientation: unknown");
                    }
                }
            }
        };

        registerReceiver(rotationReceiver, filter);

        return START_NOT_STICKY;
    }

    private void constructEncoder() {
        try {
            videoEncoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        videoEncoder.setCallback(new MediaCodec.Callback() {
            @Override
            public void onInputBufferAvailable(MediaCodec codec, int index) {
                throw new UnsupportedOperationException();
            }

            @Override
            public void onOutputBufferAvailable(MediaCodec codec, int index, MediaCodec.BufferInfo info) {
                ByteBuffer buffer = codec.getOutputBuffer(index);

                try {
                    Log.e(TAG, "Writing buffer of size " + buffer.remaining());
                    channel.write(buffer);
                    fos.flush();
                    codec.releaseOutputBuffer(index, false);
                } catch (IOException e) {
                    cleanup();
                }
            }

            @Override
            public void onError(MediaCodec codec, MediaCodec.CodecException e) {
                e.printStackTrace();
            }

            @Override
            public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
                Log.i(TAG, "Output format changed: " + codec + ", " + format);
            }
        });
    }

    private void cleanup() {
        projection.stop();
        videoEncoder.stop();
        videoEncoder.release();
        display.release();
        surface.release();

        try {
            fos.close();
        } catch (IOException e) {
            e.printStackTrace();
        }

        try {
            channel.close();
        } catch (IOException e) {
            e.printStackTrace();
        }

        wakeLock.release();
        stopSelf();
    }


    private MediaFormat getFormat() {
        Log.e(TAG, "Configuring encoder for "+getWidth()+"x"+getHeight());
        MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, getWidth(), getHeight());

        // Set some required properties. The media codec may fail if these aren't defined.
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 0);
        format.setInteger(MediaFormat.KEY_BIT_RATE, 8 * 1024 * 1024);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, 60);
        format.setInteger(MediaFormat.KEY_CAPTURE_RATE, 60);
        format.setInteger(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 1000000 / 60);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 600);
        return format;
    }

    private void configureEncoder() {
        display.resize(getWidth(), getHeight(), getDPI());
        videoEncoder.configure(getFormat(), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        if (surface != null) {
            surface.release();
        }
        surface = videoEncoder.createInputSurface();
        display.setSurface(surface);
        videoEncoder.start();
    }

    @Override
    public IBinder onBind(Intent intent) {
        throw new UnsupportedOperationException();
    }
}
