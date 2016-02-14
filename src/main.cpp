#include "aoa.h"

int main(int argc, char* argv[]) {
  auto device = AOADevice::open(AOAMode::audio);
  if (!device->initialize()) {
    fprintf(stderr, "failed to initialize device\n");
    return 1;
  }
}
