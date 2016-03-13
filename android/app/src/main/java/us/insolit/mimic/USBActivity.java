package us.insolit.mimic;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;

public class USBActivity extends Activity {
    MediaProjectionManager projectionManager;
    UsbAccessory accessory;

    private static final int REQUEST_CODE_OVERLAY_PERMISSION = 0;
    private static final int REQUEST_CODE_PROJECTION = 1;

    private void requestProjection() {
        projectionManager = (MediaProjectionManager)getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        String action = getIntent().getAction();
        if (UsbManager.ACTION_USB_ACCESSORY_ATTACHED.equals(action)) {
            Log.e("mimic", "Device attached");
            accessory = getIntent().getParcelableExtra(UsbManager.EXTRA_ACCESSORY);

            // Request screen capture.
            startActivityForResult(projectionManager.createScreenCaptureIntent(), REQUEST_CODE_PROJECTION);
        } else {
            Log.wtf("mimic", "Unknown intent received: {}".format(action));
            finish();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.e("mimic", "onCreate called");

        if (!Settings.canDrawOverlays(this)) {
            Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + getPackageName()));
            startActivityForResult(intent, REQUEST_CODE_OVERLAY_PERMISSION);
        } else {
            requestProjection();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_CODE_OVERLAY_PERMISSION) {
            if (!Settings.canDrawOverlays(this)) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, REQUEST_CODE_OVERLAY_PERMISSION);
            } else {
                requestProjection();
            }
        } else if (requestCode == REQUEST_CODE_PROJECTION) {
            // Pass the intent along to StreamService.
            Intent streamIntent = new Intent();
            streamIntent.setClass(this, StreamService.class);
            streamIntent.putExtra(Intent.EXTRA_INTENT, data);
            streamIntent.putExtra(UsbManager.EXTRA_ACCESSORY, accessory);
            startService(streamIntent);
            finish();
        } else {
            Log.wtf("mimic", "Unexpected request code: " + requestCode);
        }
    }
}
