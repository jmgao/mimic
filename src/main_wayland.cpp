#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include <gst/gst.h>

#include "aoa.h"
#include "chrono_literals.h"
#include "decoder.h"

static void frame_callback(GstSample* sample) {
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  GstMapInfo map_info;

  if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
    error("failed to map GstBuffer");
    exit(1);
  }

  static int frame = 0;
  char filename[sizeof("test/frame1234567890.i420")];
  sprintf(filename, "test/frame%d.i420", frame);
  int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0600);
  uint8_t* data = map_info.data;
  int count = map_info.size;
  while (count > 0) {
    ssize_t bytes_written = write(fd, data, count);
    if (bytes_written < 0) {
      error("write failed");
      exit(1);
    }
    data += bytes_written;
    count -= bytes_written;
  }
  gst_buffer_unmap(buffer, &map_info);
  close(fd);
  printf("wrote frame %d\n", frame);
  ++frame;
}

int main(int, char**) {
  std::unique_ptr<AOADevice> device;
  while (!device) {
    std::this_thread::sleep_for(100ms);
    device = AOADevice::open(AOAMode::accessory);
  }

  if (!device->initialize()) {
    error("failed to initialize device");
    return 1;
  }

  int accessory_fd = device->get_accessory_fd();
  gst_init(nullptr, nullptr);

  GMainLoop* loop = g_main_loop_new(nullptr, false);
  create_decoder(accessory_fd, frame_callback);
  g_print("Running\n");
  g_main_loop_run(loop);
}
