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
constexpr int PID_NEXUS_MTP = 0x4EE1;
constexpr int PID_NEXUS_MTP_ADB = 0x4EE2;
constexpr int PID_NEXUS_RNDIS = 0x4EE3;
constexpr int PID_NEXUS_RNDIS_ADB = 0x4EE4;
constexpr int PID_NEXUS_PTP = 0x4EE5;
constexpr int PID_NEXUS_PTP_ADB = 0x4EE6;
constexpr int PID_NEXUS_ADB = 0x4EE7;
constexpr int PID_NEXUS_MIDI = 0x4EE8;
constexpr int PID_NEXUS_MIDI_ADB = 0x4EE9;
#define PID_NEXUS_ALL                                                                    \
  PID_NEXUS_MTP, PID_NEXUS_MTP_ADB, PID_NEXUS_RNDIS, PID_NEXUS_RNDIS_ADB, PID_NEXUS_PTP, \
    PID_NEXUS_PTP_ADB, PID_NEXUS_ADB, PID_NEXUS_MIDI, PID_NEXUS_MIDI_ADB

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

static bool aoa_initialize(libusb_device_handle* handle, AOAMode mode) {
  unsigned char aoa_version_buf[2] = {};
  int rc;

  // https://source.android.com/devices/accessories/aoa.html
  rc = libusb_control_transfer(handle, USB_DIR_IN | USB_TYPE_VENDOR, 51, 0, 0, aoa_version_buf,
                               sizeof(aoa_version_buf), 0);

  if (rc < 0) {
    error("failed to initialize AoA: %s", libusb_error_name(rc));
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
      error("failed to send vendor string: %s", libusb_error_name(rc));
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
    error("failed to enable audio: %s", libusb_error_name(rc));
    return false;
  }
  return true;
}

static bool aoa_start(libusb_device_handle* handle) {
  int rc = libusb_control_transfer(handle, USB_DIR_OUT | USB_TYPE_VENDOR, 53, 0, 0, nullptr, 0, 0);
  if (rc < 0) {
    error("failed to start AoA: %s", libusb_error_name(rc));
    return false;
  }
  return true;
}

enum class usb_endpoint_iterate_result {
  proceed,
  terminate,
};

using usb_endpoint_iterate_callback_t = usb_endpoint_iterate_result (*)(
  const libusb_interface_descriptor& interface, const libusb_endpoint_descriptor& endpoint);

template <typename Callback>
static bool usb_endpoint_iterate(libusb_device_handle* handle, const Callback& callback) {
  struct libusb_config_descriptor* config;
  int rc = libusb_get_active_config_descriptor(libusb_get_device(handle), &config);
  if (rc != 0) {
    error("failed to get active config descriptor");
    return false;
  }
  Auto(libusb_free_config_descriptor(config));

  for (size_t i = 0; i < config->bNumInterfaces; ++i) {
    const libusb_interface& interface = config->interface[i];
    for (ssize_t j = 0; j < interface.num_altsetting; ++j) {
      const libusb_interface_descriptor& interface_descriptor = interface.altsetting[j];
      for (size_t k = 0; k < interface_descriptor.bNumEndpoints; ++k) {
        const libusb_endpoint_descriptor& endpoint = interface_descriptor.endpoint[k];
        switch (callback(interface_descriptor, endpoint)) {
          case usb_endpoint_iterate_result::proceed:
            continue;

          case usb_endpoint_iterate_result::terminate:
            return true;
        }
      }
    }
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
      error("failed to get connected devices: %s", libusb_error_name(device_count));
      return nullptr;
    }

    for (int i = 0; i < device_count; ++i) {
      libusb_device* device = devices[i];
      struct libusb_device_descriptor descriptor;
      rc = libusb_get_device_descriptor(device, &descriptor);
      if (rc != 0) {
        error("failed to get device descriptor: %s", libusb_error_name(rc));
        return nullptr;
      }

      if (descriptor.idVendor == VID_GOOGLE) {
        info("found device %x:%x", descriptor.idVendor, descriptor.idProduct);
        auto it = std::find(accepted_pids.cbegin(), accepted_pids.cend(), descriptor.idProduct);
        if (it != accepted_pids.cend()) {
          libusb_device_handle* handle;

          rc = libusb_open(device, &handle);
          if (rc != 0) {
            error("failed to open device: %s", libusb_error_name(rc));
            return nullptr;
          }
          return handle;
        } else {
          info("failed to match device to accepted PIDs");
        }
      }
    }

    std::this_thread::sleep_for(100ms);
  }

  debug("timeout elapsed while waiting for device");
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

  libusb_device_handle* handle = open_device_timeout({ PID_NEXUS_ALL }, 100ms);
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

  accessory_internal_fd = sfd[0];
  accessory_external_fd = sfd[1];

  // Find the accessory endpoints.
  int sink = 0;
  int source = 0;

  auto iterate_callback = [&sink, &source](const libusb_interface_descriptor& interface,
                                           const libusb_endpoint_descriptor& endpoint) {
    if (interface.bInterfaceClass != 255 || interface.bInterfaceSubClass != 255 ||
        interface.bInterfaceProtocol != 0) {
      return usb_endpoint_iterate_result::proceed;
    }

    if ((endpoint.bEndpointAddress & 0x80) == 0) {
      if (sink != 0) {
        error("multiple sink endpoints found");
        exit(1);
      }
      sink = endpoint.bEndpointAddress;
    } else {
      if (source != 0) {
        error("multiple source endpoints found");
        exit(1);
      }
      source = endpoint.bEndpointAddress;
    }

    return usb_endpoint_iterate_result::proceed;
  };

  if (!usb_endpoint_iterate(handle, iterate_callback)) {
    error("failed to iterate across USB endpoints");
    exit(1);
  }

  if (sink == 0) {
    error("failed to find sink endpoint");
    exit(0);
  } else if (source == 0) {
    error("failed to find source endpoint");
    exit(0);
  }

  debug("found AoA device endpoints: sink=%#x, source = %#x", sink, source);

  auto read_function = [this, source]() {
    while (true) {
      unsigned char buffer[16384];
      int transferred;
      int rc = libusb_bulk_transfer(handle, source, buffer, sizeof(buffer), &transferred, 0);
      if (rc != 0) {
        error("failed to transfer data from AoA endpoint: %s", libusb_error_name(rc));
        exit(1);
      }

      debug("transferring %d bytes from usb to local", transferred);
      const char* current = reinterpret_cast<char*>(buffer);
      while (transferred > 0) {
        ssize_t written = write(accessory_internal_fd, current, transferred);
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

  auto write_function = [this, sink]() {
    while (true) {
      char buffer[16384];
      ssize_t bytes_read = read(accessory_internal_fd, buffer, sizeof(buffer));
      if (bytes_read < 0) {
        error("read failed: %s", strerror(errno));
        exit(1);
      }

      debug("transferring %zd bytes from local to usb", bytes_read);
      unsigned char* current = reinterpret_cast<unsigned char*>(buffer);
      while (bytes_read > 0) {
        int transferred;
        int rc = libusb_bulk_transfer(handle, sink, current, bytes_read, &transferred, 0);
        if (rc != 0) {
          error("failed to transfer data to AoA endpoint: %s", libusb_error_name(rc));
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

  this->accessory_read_thread = std::thread(read_function);
  this->accessory_write_thread = std::thread(write_function);

  return true;
}
