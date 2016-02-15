#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "aoa.h"

int main(int argc, char* argv[]) {
  auto device = AOADevice::open(AOAMode::audio | AOAMode::accessory);
  if (!device) {
    error("failed to find device");
    return 1;
  }

  if (!device->initialize()) {
    error("failed to initialize device");
    return 1;
  }

  while (true) {
    char buf[16385];
    ssize_t rc = read(device->get_accessory_fd(), buf, sizeof(buf) - 1);
    if (rc < 0) {
      error("local read failed: %s", strerror(errno));
    }
    buf[rc] = '\0';
    printf("read %zd bytes: %s\n", rc, buf);
  }
}
