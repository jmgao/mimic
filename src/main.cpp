#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <gst/gst.h>

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

  int accessory_fd = device->get_accessory_fd();

  // Play the stream.
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;
  gst_init(nullptr, nullptr);
  dup2(accessory_fd, 123);

  pipeline = gst_parse_launch("fdsrc fd=123 ! h264parse ! avdec_h264 ! autovideosink sync=false", nullptr);
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  bus = gst_element_get_bus(pipeline);
  msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

  if (msg) {
    gst_message_unref(msg);
  }
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return 0;
}
