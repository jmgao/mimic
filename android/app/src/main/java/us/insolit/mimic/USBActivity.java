package us.insolit.mimic;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.media.projection.MediaProjectionManager;
import android.os.Bundle;
import android.util.Log;

public class USBActivity extends Activity {
    MediaProjectionManager projectionManager;
    UsbAccessory accessory;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.e("mimic", "onCreate called");
        projectionManager = (MediaProjectionManager)getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        String action = getIntent().getAction();
        if (UsbManager.ACTION_USB_ACCESSORY_ATTACHED.equals(action)) {
            Log.e("mimic", "Device attached");
            accessory = getIntent().getParcelableExtra(UsbManager.EXTRA_ACCESSORY);

            // Request screen capture.
            startActivityForResult(projectionManager.createScreenCaptureIntent(), 0);
        } else {
            Log.wtf("mimic", "Unknown intent received: {}".format(action));
            finish();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        // Pass the intent along to StreamService.
        Intent streamIntent = new Intent();
        streamIntent.setClass(this, StreamService.class);
        streamIntent.putExtra(Intent.EXTRA_INTENT, data);
        streamIntent.putExtra(UsbManager.EXTRA_ACCESSORY, accessory);
        startService(streamIntent);
        finish();
    }
}
