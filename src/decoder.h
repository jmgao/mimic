#pragma once

#include <gst/gst.h>

using decoder_callback_t = void (*)(GstSample*);
void create_decoder(int h264_fd, decoder_callback_t callback);
