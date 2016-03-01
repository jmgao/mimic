#include <stdlib.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include "decoder.h"
#include "log.h"

void create_decoder(int h264_fd, decoder_callback_t callback) {
  GstElement* pipeline = gst_pipeline_new("mimic-video");
  GstElement* fdsrc = gst_element_factory_make("fdsrc", "mimic-video-fdsrc");
  g_object_set(fdsrc, "fd", h264_fd, nullptr);

  GstElement* parser = gst_element_factory_make("h264parse", "h264parse");

  GstElement* avdec_h264 = gst_element_factory_make("avdec_h264", "avdec_h264");
  gst_video_decoder_set_output_state(reinterpret_cast<GstVideoDecoder*>(avdec_h264),
                                     GST_VIDEO_FORMAT_RGB, 800, 480, nullptr);

  GstElement* capsfilter = gst_element_factory_make("capsfilter", "mimic-video-capsfilter");
  const char* format = "video/x-raw,format=I420";
  GstCaps* caps = gst_caps_from_string(format);
  g_object_set(capsfilter, "caps", caps, nullptr);

  GstElement* appsink = gst_element_factory_make("appsink", "mimic-video-appsink");
  GstAppSinkCallbacks callbacks = {};
  callbacks.new_sample = [](GstAppSink* sink, gpointer callback) {
    GstSample* sample = gst_app_sink_pull_sample(sink);
    reinterpret_cast<decoder_callback_t>(callback)(sample);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  };
  gst_app_sink_set_callbacks(reinterpret_cast<GstAppSink*>(appsink), &callbacks,
                             reinterpret_cast<void*>(callback), nullptr);

  if (!pipeline || !fdsrc || !parser || !avdec_h264 || !capsfilter || !appsink) {
    error("failed to create pipeline, aborting");
    exit(1);
  }

  gst_bin_add_many(GST_BIN(pipeline), fdsrc, parser, avdec_h264, capsfilter, appsink, nullptr);
  gst_element_link_many(fdsrc, parser, avdec_h264, capsfilter, appsink, nullptr);
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
}
