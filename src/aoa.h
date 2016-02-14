#pragma once

#include <memory>
#include <stdio.h>

#include <libusb.h>

enum class AOAMode {
  accessory = 1 << 0,
  audio = 1 << 1,
};

AOAMode operator|(const AOAMode& lhs, const AOAMode& rhs);
AOAMode operator&(const AOAMode& lhs, const AOAMode& rhs);

class AOADevice {
 private:
  libusb_device_handle* handle;
  AOAMode mode;

  AOADevice(libusb_device_handle* handle, AOAMode mode);

 public:
  ~AOADevice();

  bool initialize();
  static std::unique_ptr<AOADevice> open(AOAMode mode);
};
