#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <libusb.h>

#include "aoa.h"
#include "auto.h"

using namespace std::chrono;

constexpr int VID_GOOGLE = 0x18D1;
constexpr int PID_HAMMERHEAD = 0x4EE2;
constexpr int PID_ANGLER = 0x4EE7;

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

constexpr char MANUFACTURER[] = "jmgao";
constexpr char MODEL[] = "aha";
constexpr char DESCRIPTION[] = "Android Host Audio";
constexpr char VERSION[] = "0.0.1";
constexpr char URI[] = "https://insolit.us/aha";
constexpr char SERIAL[] = "0";

static void usb_error(int error) {
  fprintf(stderr, "error: %s\n", libusb_error_name(error));
};

static bool aoa_initialize(libusb_device_handle* handle, AOAMode mode) {
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

  auto sendString = [handle](int string_id, const std::string& string) {
    int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 52, 0, string_id,
                                     (unsigned char*)(string.c_str()), string.length() + 1, 0);
    if (rc < 0) {
      usb_error(rc);
      return false;
    }
    return true;
  };

  // When not in accessory mode, don't send the manufacturer or model strings to avoid prompting.
  // https://source.android.com/devices/accessories/aoa2.html
  //
  // TODO: Make these strings configurable.
  if (!(sendString(AOA_STRING_DESCRIPTION, DESCRIPTION) && sendString(AOA_STRING_VERSION, VERSION) &&
        sendString(AOA_STRING_URI, URI) && sendString(AOA_STRING_SERIAL, SERIAL))) {
    return false;
  }

  if ((mode & AOAMode::accessory) == AOAMode::accessory) {
    return sendString(AOA_STRING_MANUFACTURER, MANUFACTURER) && sendString(AOA_STRING_MODEL, MODEL);
  }

  return true;
}

static bool aoa_enable_audio(libusb_device_handle* handle) {
  int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 58, 1, 0, nullptr, 0, 0);
  if (rc < 0) {
    usb_error(rc);
    return false;
  }
  return true;
}

static bool aoa_start(libusb_device_handle* handle) {
  int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 53, 0, 0, nullptr, 0, 0);
  if (rc < 0) {
    usb_error(rc);
    return false;
  }
  return true;
}

static libusb_device_handle* open_device_timeout(std::vector<int> accepted_pids,
                                                 milliseconds timeout) {
  int rc;
  auto start = std::chrono::steady_clock::now();

  // TODO: Make this less dumb.
  while (std::chrono::steady_clock::now() - start < timeout) {
    libusb_device** devices;
    ssize_t device_count = libusb_get_device_list(nullptr, &devices);
    Auto(libusb_free_device_list(devices, true));

    if (device_count < 0) {
      usb_error(device_count);
      return nullptr;
    }

    for (int i = 0; i < device_count; ++i) {
      libusb_device* device = devices[i];
      struct libusb_device_descriptor descriptor;
      rc = libusb_get_device_descriptor(device, &descriptor);
      if (rc != 0) {
        usb_error(rc);
        return nullptr;
      }

      if (descriptor.idVendor == VID_GOOGLE) {
        printf("found %#X\n", descriptor.idProduct);
        auto it = std::find(accepted_pids.cbegin(), accepted_pids.cend(), descriptor.idProduct);
        if (it != accepted_pids.cend()) {
          libusb_device_handle* handle;

          rc = libusb_open(device, &handle);
          if (rc != 0) {
            usb_error(rc);
            return nullptr;
          }
          return handle;
        } else {
          printf("failed to match\n");
        }
      }
    }

    std::this_thread::sleep_for(10ms);
  }

  fprintf(stderr, "timeout elapsed while waiting for device\n");
  return nullptr;
}

AOAMode operator|(const AOAMode& lhs, const AOAMode& rhs) {
  return AOAMode(int(lhs) | int(rhs));
}

AOAMode operator&(const AOAMode& lhs, const AOAMode& rhs) {
  return AOAMode(int(lhs) & int(rhs));
}

AOADevice::AOADevice(libusb_device_handle* handle, AOAMode mode) : handle(handle), mode(mode) {
}

AOADevice::~AOADevice() {
  if (handle) {
    libusb_close(handle);
  }
}

bool AOADevice::initialize() {
  if (libusb_claim_interface(handle, 0) != 0) {
    fprintf(stderr, "failed to claim interface\n");
    return false;
  }

  if (!aoa_initialize(handle, mode)) {
    fprintf(stderr, "failed to initialize android accessory\n");
    return false;
  };

  if ((mode & AOAMode::audio) == AOAMode::audio) {
    if (!aoa_enable_audio(handle)) {
      fprintf(stderr, "failed to enable USB audio\n");
      return false;
    }
  }

  if (!aoa_start(handle)) {
    fprintf(stderr, "failed to start android accessory\n");
    return false;
  }

  libusb_close(handle);

  std::vector<int> expected_pids{ 0x2D00, 0x2D01, 0x2D02, 0x2D03, 0x2D04, 0x2D05 };
  handle = open_device_timeout(expected_pids, 5s);

  if (!handle) {
    return false;
  }
  return true;
}

std::unique_ptr<AOADevice> AOADevice::open(AOAMode mode) {
  static std::once_flag once;
  std::call_once(once, []() {
    libusb_init(nullptr);
    libusb_set_debug(nullptr, LIBUSB_LOG_LEVEL_WARNING);
  });

  libusb_device_handle* handle = open_device_timeout({ PID_HAMMERHEAD, PID_ANGLER }, 100ms);
  if (!handle) {
    return nullptr;
  }

  std::unique_ptr<AOADevice> device(new AOADevice(handle, mode));
  return std::move(device);
}
