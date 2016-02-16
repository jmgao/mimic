#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <libusb.h>

#include "aoa.h"
#include "auto.h"
#include "chrono_literals.h"

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
constexpr char MODEL[] = "mimic";
constexpr char DESCRIPTION[] = "Android USB mirror";
constexpr char VERSION[] = "0.0.1";
constexpr char URI[] = "https://insolit.us/mimic";
constexpr char SERIAL[] = "0";

static void usb_error(int error) {
  error("%s", libusb_error_name(error));
};

static bool aoa_initialize(libusb_device_handle* handle, AOAMode mode) {
  unsigned char aoa_version_buf[2] = {};
  int rc;

  // https://source.android.com/devices/accessories/aoa.html
  rc = libusb_control_transfer(handle, USB_DIR_IN | USB_TYPE_VENDOR, 51, 0, 0, aoa_version_buf,
                               sizeof(aoa_version_buf), 0);

  if (rc < 0) {
    usb_error(rc);
    return false;
  }

  uint16_t aoa_version;
  memcpy(&aoa_version, aoa_version_buf, sizeof(aoa_version_buf));
  if (aoa_version != 2) {
    error("unsupported AOA protocol version %u", aoa_version);
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
                                                 std::chrono::milliseconds timeout) {
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
        info("found device with VID %#x", descriptor.idVendor);
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
          info("failed to match device to accepted PIDs");
        }
      }
    }

    std::this_thread::sleep_for(1s);
  }

  error("timeout elapsed while waiting for device");
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
  if (!aoa_initialize(handle, mode)) {
    error("failed to initialize android accessory");
    return false;
  };

  if ((mode & AOAMode::audio) == AOAMode::audio) {
    if (!aoa_enable_audio(handle)) {
      error("failed to enable USB audio");
      return false;
    }
  }

  if (!aoa_start(handle)) {
    error("failed to start android accessory");
    return false;
  }

  libusb_close(handle);

  std::vector<int> expected_pids{ 0x2D00, 0x2D01, 0x2D02, 0x2D03, 0x2D04, 0x2D05 };
  handle = open_device_timeout(expected_pids, 5s);

  if (!handle) {
    return false;
  }

  if ((mode & AOAMode::accessory) == AOAMode::accessory) {
    if (!spawn_accessory_threads()) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<AOADevice> AOADevice::open(AOAMode mode) {
  static std::once_flag once;
  std::call_once(once, []() {
    libusb_init(nullptr);
  });

  libusb_device_handle* handle = open_device_timeout({ PID_HAMMERHEAD, PID_ANGLER }, 100ms);
  if (!handle) {
    return nullptr;
  }

  std::unique_ptr<AOADevice> device(new AOADevice(handle, mode));
  return std::move(device);
}

bool AOADevice::spawn_accessory_threads() {
  int sfd[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sfd) != 0) {
    fprintf(stderr, "failed to create socketpair: %s\n", strerror(errno));
    return false;
  }

  internal_socket = sfd[0];
  external_socket = sfd[1];

  constexpr int read_endpoint = 0x81;
  constexpr int write_endpoint = 0x02;
  auto read_function = [this]() {
    while (true) {
      unsigned char buffer[16384];
      int transferred;
      int rc = libusb_bulk_transfer(handle, read_endpoint, buffer, sizeof(buffer), &transferred, 0);
      if (rc != 0) {
        usb_error(rc);
        exit(1);
      }

      debug("transferring %d bytes from usb to local", transferred);
      const char* current = reinterpret_cast<char*>(buffer);
      while (transferred > 0) {
        ssize_t written = write(internal_socket, current, transferred);
        if (written < 0) {
          error("write failed: %s", strerror(errno));
          exit(1);
        } else if (written == 0) {
          error("write returned EOF");
          exit(1);
        }

        current += written;
        transferred -= written;
      }
    }
  };

  auto write_function = [this]() {
    while (true) {
      char buffer[16384];
      ssize_t bytes_read = read(internal_socket, buffer, sizeof(buffer));
      if (bytes_read < 0) {
        error("read failed: %s", strerror(errno));
        exit(1);
      }

      debug("transferring %zd bytes from local to usb", bytes_read);
      unsigned char* current = reinterpret_cast<unsigned char*>(buffer);
      while (bytes_read > 0) {
        int transferred;
        int rc = libusb_bulk_transfer(handle, write_endpoint, current, bytes_read, &transferred, 0);
        if (rc != 0) {
          usb_error(rc);
          exit(1);
        }

        if (transferred <= 0) {
          error("libusb_bulk_transfer transferred %d bytes", transferred);
        }

        current += transferred;
        bytes_read -= transferred;
      }
    }
  };

  read_thread = std::thread(read_function);
  write_thread = std::thread(write_function);

  return true;
}
