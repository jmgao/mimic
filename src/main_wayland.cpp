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

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "aoa.h"
#include "chrono_literals.h"
#include "decoder.h"

constexpr int WIDTH = 800;
constexpr int HEIGHT = 480;

static wl_display* display;
static wl_compositor* compositor;
static wl_surface* surface;
static wl_egl_window* egl_window;
static wl_region* region;
static wl_shell* shell;
static wl_shell_surface* shell_surface;

static EGLDisplay egl_display;
static EGLConfig egl_conf;
static EGLSurface egl_surface;
static EGLContext egl_context;

static void global_registry_handler(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
static void global_registry_remover(void*, struct wl_registry*, uint32_t);

static const struct wl_registry_listener registry_listener = { global_registry_handler,
                                                               global_registry_remover };

static void global_registry_handler(void* data, struct wl_registry* registry, uint32_t id,
                                    const char* interface, uint32_t version) {
  info("received registry event for %s id %d", interface, id);
  if (strcmp(interface, "wl_compositor") == 0) {
    void* interface = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    compositor = reinterpret_cast<wl_compositor*>(interface);
  } else if (strcmp(interface, "wl_shell") == 0) {
    shell = reinterpret_cast<wl_shell*>(wl_registry_bind(registry, id, &wl_shell_interface, 1));
  }
}

static void global_registry_remover(void* data, struct wl_registry* registry, uint32_t id) {
  info("received a registry remove event for %d", id);
}

static void get_server_references(void) {
  display = wl_display_connect(NULL);
  if (display == NULL) {
    fatal("failed to connect to display");
  }

  struct wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);

  wl_display_dispatch(display);
  wl_display_roundtrip(display);

  if (compositor == NULL || shell == NULL) {
    fatal("failed to find compositor or shell");
  }
}

static void create_opaque_region() {
  region = wl_compositor_create_region(compositor);
  wl_region_add(region, 0, 0, WIDTH, HEIGHT);
  wl_surface_set_opaque_region(surface, region);
}

static const char* egl_strerror(EGLenum error) {
  switch (error) {
    case EGL_BAD_DISPLAY:
      return "EGL_BAD_DISPLAY";

    case EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";

    case EGL_BAD_SURFACE:
      return "EGL_BAD_SURFACE";

    case EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";

    case EGL_BAD_MATCH:
      return "EGL_BAD_MATCH";

    case EGL_BAD_ACCESS:
      return "EGL_BAD_ACCESS";

    case EGL_BAD_NATIVE_PIXMAP:
      return "EGL_BAD_NATIVE_PIXMAP";

    case EGL_BAD_NATIVE_WINDOW:
      return "EGL_BAD_NATIVE_WINDOW";

    case EGL_BAD_CURRENT_SURFACE:
      return "EGL_BAD_CURRENT_SURFACE";

    case EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";

    case EGL_CONTEXT_LOST:
      return "EGL_CONTEXT_LOST";
  }
}

static void initialize_egl() {
  EGLint major, minor, count, n, size;
  EGLConfig* configs;
  int i;
  EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                              EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };

  static const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

  egl_display = eglGetDisplay((EGLNativeDisplayType)display);
  if (egl_display == EGL_NO_DISPLAY) {
    fatal("failed to create egl display");
  }

  if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
    fatal("failed to initialize egl display");
  }

  info("EGL major: %d, minor %d", major, minor);

  eglGetConfigs(egl_display, NULL, 0, &count);
  configs = new EGLConfig[count]();

  eglChooseConfig(egl_display, config_attribs, configs, count, &n);

  for (i = 0; i < n; i++) {
    eglGetConfigAttrib(egl_display, configs[i], EGL_BUFFER_SIZE, &size);
    info("Buffer size for config %d is %d", i, size);
    eglGetConfigAttrib(egl_display, configs[i], EGL_RED_SIZE, &size);
    info("Red size for config %d is %d", i, size);

    // just choose the first one
    egl_conf = configs[i];
    break;
  }

  egl_context = eglCreateContext(egl_display, egl_conf, EGL_NO_CONTEXT, context_attribs);
}

static void create_window() {
  egl_window = wl_egl_window_create(surface, WIDTH, HEIGHT);
  if (egl_window == EGL_NO_SURFACE) {
    fatal("failed to create egl window");
  }

  egl_surface = eglCreateWindowSurface(egl_display, egl_conf, egl_window, NULL);

  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    fatal("failed to make surface current");
  }
}

#define check_error()                                                   \
  do {                                                                  \
    bool should_exit = false;                                           \
    GLenum error;                                                       \
    while ((error = glGetError()) != GL_NO_ERROR) {                     \
      should_exit = true;                                               \
      fprintf(stderr, "error[%d]: %s\n", __LINE__, gl_strerror(error)); \
    }                                                                   \
    if (should_exit) exit(1);                                           \
  } while (0)

#define GLSL(src) "#version 100\n" #src

static GLuint texture;
static GLuint shaderProgram;

static const char* gl_strerror(GLenum error) {
  switch (error) {
    case GL_INVALID_ENUM:
      return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
      return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
      return "GL_INVALID_OPERATION";
    default:
      return "GL_UNKNOWN";
  }
}

static void initialize_opengl() {
  // Create a Vertex Buffer Object and copy the vertex data to it
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  check_error();

  float vertices[] = {
    //  Position      Texcoords
    -1.0f, 1.0f,  0.0f, 0.0f,  // Top-left
    1.0f,  1.0f,  1.0f, 0.0f,  // Top-right
    1.0f,  -1.0f, 1.0f, 1.0f,  // Bottom-right
    -1.0f, -1.0f, 0.0f, 1.0f   // Bottom-left
  };

  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  check_error();

  // Create an Element Buffer Object and copy the element data to it
  GLuint ebo;
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  check_error();

  GLuint elements[] = { 0, 1, 2, 2, 3, 0 };

  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elements), elements, GL_STATIC_DRAW);

  auto check_shader_compile = [](GLuint shader) {
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
      char buffer[4096];
      glGetShaderInfoLog(shader, sizeof(buffer), NULL, buffer);
      error("failed to compile shader:");
      printf("%s\n", buffer);
      exit(1);
    }
  };

  // Create and compile the vertex shader
  const char* vertexSource = GLSL(
      attribute vec2 position;
      attribute vec2 texcoord;

      varying vec2 v_texcoord;

      void main() {
          gl_Position = vec4(position, 0.0, 1.0);
          v_texcoord = texcoord;
      }
  );

  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexSource, NULL);
  glCompileShader(vertexShader);
  check_shader_compile(vertexShader);

  // Create and compile the fragment shader
  #include "yuv2rgb.shader"

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &yub2rgb, NULL);
  glCompileShader(fragmentShader);
  check_shader_compile(fragmentShader);

  // Link the vertex and fragment shader into a shader program
  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);
  glUseProgram(shaderProgram);
  check_error();

  // Create the texture
  char buf[WIDTH * HEIGHT * 3 / 2] = {};

  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &texture);
  check_error();
  glBindTexture(GL_TEXTURE_2D, texture);
  check_error();
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, WIDTH, HEIGHT * 3 / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, buf);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  check_error();

  check_error();
  glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);
  glUniform1f(glGetUniformLocation(shaderProgram, "imageWidth"), float(WIDTH));
  glUniform1f(glGetUniformLocation(shaderProgram, "imageHeight"), float(HEIGHT));
  check_error();

  // Specify the layout of the vertex data
  GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
  check_error();
  glEnableVertexAttribArray(posAttrib);
  check_error();
  glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
  check_error();

  GLint texAttrib = glGetAttribLocation(shaderProgram, "texcoord");
  check_error();
  glEnableVertexAttribArray(texAttrib);
  check_error();
  glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                        (void*)(2 * sizeof(GLfloat)));
  check_error();
}

static void draw_frame(const void* frame_data) {
  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    fatal("failed to make surface current: %s", egl_strerror(eglGetError()));
  }

  glActiveTexture(GL_TEXTURE0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT * 3 / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame_data);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  eglSwapBuffers(egl_display, egl_surface);

  if (!eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
    fatal("failed to release surface: %s", egl_strerror(eglGetError()));
  }
}

static void frame_callback(GstSample* sample) {
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  GstMapInfo map_info;

  if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
    fatal("failed to map GstBuffer");
  }

  draw_frame(map_info.data);

#ifdef DUMP_FRAME
  static int frame = 0;
  char filename[sizeof("test/frame1234567890.yuv")];
  sprintf(filename, "test/frame%d.yuv", frame);
  int fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, 0600);
  uint8_t* data = map_info.data;
  int count = map_info.size;
  while (count > 0) {
    ssize_t bytes_written = write(fd, data, count);
    if (bytes_written < 0) {
      fatal("write failed");
    }
    data += bytes_written;
    count -= bytes_written;
  }
  close(fd);
  printf("wrote frame %d\n", frame);
  ++frame;
#endif

  gst_buffer_unmap(buffer, &map_info);
}

static void read_splash(char* splash) {
  int fd = open("splash.yuv", O_RDONLY);
  if (fd < 0) {
    return;
  }

  size_t bytes_left = WIDTH * HEIGHT * 3 / 2;
  char* cur = splash;
  while (bytes_left > 0) {
    ssize_t bytes_read = read(fd, cur, bytes_left);
    if (bytes_read < 0) {
      fatal("read failed: %s", strerror(errno));
    }
    cur += bytes_read;
    bytes_left -= bytes_read;
  }
  close(fd);
}

int main(int argc, char** argv) {
  get_server_references();

  surface = wl_compositor_create_surface(compositor);
  if (surface == NULL) {
    fatal("failed to create surface");
  }

  shell_surface = wl_shell_get_shell_surface(shell, surface);
  wl_shell_surface_set_toplevel(shell_surface);

  create_opaque_region();
  initialize_egl();
  create_window();
  initialize_opengl();

  char splash[WIDTH * HEIGHT * 3];
  memset(splash, 0xff, sizeof(splash));
  read_splash(splash);
  draw_frame(splash);

  GMainLoop* loop = g_main_loop_new(nullptr, false);
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

  create_decoder(accessory_fd, frame_callback);
  g_main_loop_run(loop);
}
