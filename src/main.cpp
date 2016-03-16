#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "aoa.h"
#include "chrono_literals.h"

static pid_t video_pid = -1;
static pid_t audio_pid = -1;

static void reap() {
  if (video_pid > 0) {
    warn("Reaping child %d", video_pid);
    kill(video_pid, SIGINT);
  }
  if (audio_pid > 0) {
    warn("Reaping child %d", audio_pid);
    kill(audio_pid, SIGINT);
  }
}

static int exec_gstreamer(int accessory_fd, int audio_fd) {
  video_pid = fork();
  if (video_pid < 0) {
    error("video fork failed: %s", strerror(errno));
    return 1;
  }

  if (video_pid == 0) {
    dup2(accessory_fd, STDIN_FILENO);
#ifdef M3_CROSS
    execlp("gst-launch", "gst-launch", "fdsrc", "!",
           "video/x-h264,width=800,height=480,framerate=60/1", "!", "vpudec", "!", "mfw_v4lsink",
           "sync=false", nullptr);
#else
    execlp("gst-launch-1.0", "gst-launch-1.0", "fdsrc", "!", "h264parse", "!", "avdec_h264", "!",
           "autovideosink", "sync=false", nullptr);
#endif
    error("exec failed: %s", strerror(errno));
    quick_exit(1);
  }

  audio_pid = fork();
  if (audio_pid < 0) {
    error("audio fork failed: %s", strerror(errno));
    return 1;
  }

  if (audio_pid == 0) {
    dup2(audio_fd, STDIN_FILENO);
    execlp("gst-launch-0.10", "gst-launch-0.10", "fdsrc", "!",
           "audio/x-raw-int,width=16,depth=16,endianness=1234,channels=2,rate=44100,signed=true",
           "!", "audioconvert", "!", "autoaudiosink", "sync=false", nullptr);
    error("exec failed: %s", strerror(errno));
    quick_exit(1);
  }

  atexit(reap);

  int status;
  if (waitpid(video_pid, &status, 0) != video_pid) {
    error("waitpid failed: %s", strerror(errno));
    return 1;
  }
  video_pid = -1;

  if (waitpid(audio_pid, &status, 0) != video_pid) {
    error("waitpid failed: %s", strerror(errno));
    return 1;
  }
  audio_pid = -1;

  return 0;
}

int main(int argc, char* argv[]) {
  std::unique_ptr<AOADevice> device;
  while (!device) {
    std::this_thread::sleep_for(100ms);
    device = AOADevice::open(AOAMode::accessory | AOAMode::audio);
  }

  if (!device->initialize()) {
    error("failed to initialize device");
    return 1;
  }

  int accessory_fd = device->get_accessory_fd();
  int audio_fd = device->get_audio_fd();

  return exec_gstreamer(accessory_fd, audio_fd);
}
