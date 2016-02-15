#pragma once

#include <stdio.h>

#include <memory>
#include <thread>

#include <libusb.h>

#define _log(fmt, prefix, ...) fprintf(stderr, prefix fmt "\n", ##__VA_ARGS__)
#define log(fmt, ...) _log(fmt, "", ##__VA_ARGS__)
#define debug(fmt, ...) _log(fmt, "debug: ", ##__VA_ARGS__)
#define info(fmt, ...) _log(fmt, "info: ", ##__VA_ARGS__)
#define warn(fmt, ...) _log(fmt, "warning: ", ##__VA_ARGS__)
#define error(fmt, ...) _log(fmt, "error: ", ##__VA_ARGS__)

enum class AOAMode {
  accessory = 1 << 0,
  audio = 1 << 1,
};

AOAMode operator|(const AOAMode& lhs, const AOAMode& rhs);
AOAMode operator&(const AOAMode& lhs, const AOAMode& rhs);

class AOADevice {
 private:
  libusb_device_handle* handle = nullptr;
  AOAMode mode = AOAMode(0);
  std::thread read_thread;
  std::thread write_thread;
  int internal_socket = -1;
  int external_socket = -1;

  AOADevice(libusb_device_handle* handle, AOAMode mode);

 public:
  ~AOADevice();

  bool initialize();
  static std::unique_ptr<AOADevice> open(AOAMode mode);

  int get_accessory_fd() {
    return external_socket;
  }

 private:
  bool spawn_accessory_threads();
};
