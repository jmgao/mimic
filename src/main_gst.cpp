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

static pid_t child_pid = -1;

static void reap() {
  if (child_pid > 0) {
    warn("Reaping child %d", child_pid);
    kill(child_pid, SIGINT);
  }
}

static int exec_gstreamer(int accessory_fd) {
  child_pid = fork();
  if (child_pid < 0) {
    error("fork failed: %s", strerror(errno));
    return 1;
  }

  if (child_pid == 0) {
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

  atexit(reap);
  int status;
  if (waitpid(child_pid, &status, 0) != child_pid) {
    error("waitpid failed: %s", strerror(errno));
    return 1;
  }

  child_pid = -1;
  return WEXITSTATUS(status);
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
  return exec_gstreamer(accessory_fd);
}
