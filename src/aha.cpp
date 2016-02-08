#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <libusb.h>

#include "aha.h"
#include "auto.h"

constexpr int VID = 0x18D1;
constexpr int PID = 0x4EE7; // Angler

// TODO: These should be in libusb.h somewhere?
constexpr int USB_DIR_IN = 0x80;
constexpr int USB_DIR_OUT = 0x00;
constexpr int USB_TYPE_VENDOR = 0x40;

// TODO: enumify, also the whole of this should probably be encapsulated better.
constexpr int AOA_STRING_MANUFACTURER = 0;
constexpr int AOA_STRING_MODEL = 1;
constexpr int AOA_STRING_DESCRIPTION = 2;
constexpr int AOA_STRING_VERSION = 3;
constexpr int AOA_STRING_URI = 4;
constexpr int AOA_STRING_SERIAL = 5;

constexpr char DESCRIPTION[] = "Android Host Audio";
constexpr char VERSION[] = "0.0.1";
constexpr char URI[] = "https://insolit.us/aha";
constexpr char SERIAL[] = "0";

void usb_error(int error) {
  fprintf(stderr, "error: %s\n", libusb_error_name(error));
};

bool aoa_initialize(libusb_device_handle* handle) {
  unsigned char aoa_version_buf[4];
	int rc;

  // https://source.android.com/devices/accessories/aoa.html
  rc = libusb_control_transfer(handle, USB_DIR_IN | USB_TYPE_VENDOR, 51, 0, 0, aoa_version_buf,
                               sizeof(aoa_version_buf), 0);

  if (rc < 0) {
    usb_error(rc);
    return false;
  }

  uint32_t aoa_version;
  memcpy(&aoa_version, aoa_version_buf, sizeof(aoa_version_buf));
  if (aoa_version != 2) {
    fprintf(stderr, "error: unsupported AOA protocol version %" PRIu32 "\n", aoa_version);
    return false;
  }

  /* https://source.android.com/devices/accessories/aoa2.html
   * You can design an accessory (such as an audio dock) that uses audio and HID support but does
   * not communicate with an application on the Android device. For these accessories, users do not
   * need to receive dialog prompts for finding and associating the newly attached accessory with an
   * Android application that can communicate with it.
   *
   * To suppress such dialogs after an accessory connects, the accessory can choose not to send the
   * manufacturer and model names to the Android device.
   */

  auto sendString = [handle](int string_id, const std::string& string) {
    int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 52, 0, string_id,
                                     (unsigned char*)(string.c_str()),
                                     string.length() + 1, 0);
    if (rc < 0) {
      usb_error(rc);
      return false;
    }
    return true;
  };

  if (!(sendString(AOA_STRING_DESCRIPTION, DESCRIPTION) && sendString(AOA_STRING_VERSION, VERSION) &&
        sendString(AOA_STRING_URI, URI) && sendString(AOA_STRING_SERIAL, SERIAL))) {
    return false;
  }
  return true;
}

bool aoa_set_audio_mode(libusb_device_handle* handle, bool on) {
  int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 58, on, 0, nullptr, 0, 0);
  if (rc < 0) {
    usb_error(rc);
    return false;
  }
  return true;
}

bool aoa_start(libusb_device_handle* handle) {
  int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 53, 0, 0, nullptr, 0, 0);
  if (rc < 0) {
    usb_error(rc);
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  libusb_init(nullptr);
  libusb_set_debug(nullptr, LIBUSB_LOG_LEVEL_WARNING);
  Auto(libusb_exit(nullptr));

  libusb_device_handle *handle = libusb_open_device_with_vid_pid(nullptr, VID, PID);
  if (!handle) {
    fprintf(stderr, "failed to open device\n");
    return 1;
  }
  Auto(libusb_close(handle));

	if (libusb_claim_interface(handle, 0) != 0) {
    fprintf(stderr, "failed to claim interface\n");
    return 1;
  }
  Auto(libusb_release_interface(handle, 0));

  if (!aoa_initialize(handle)) {
    fprintf(stderr, "failed to initialize android accessory\n");
    return 1;
	};

  if (!aoa_set_audio_mode(handle, true)) {
    fprintf(stderr, "failed to enable USB audio\n");
    return 1;
  }

  if (!aoa_start(handle)) {
    fprintf(stderr, "failed to start android accessory\n");
    return 1;
  }
  
  // TODO: Loop with a timeout
  usleep(1000000);
  libusb_device_handle *audio_handle = libusb_open_device_with_vid_pid(nullptr, VID, 0x2D03);
  if (!audio_handle) {
    fprintf(stderr, "failed to open audio device\n");
    return 1;
  }
  Auto(libusb_close(audio_handle));

  printf("successfully opened audio device\n");
	return 0;
}


