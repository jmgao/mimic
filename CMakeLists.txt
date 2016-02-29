cmake_minimum_required(VERSION 2.8.9)
project(mimic)

set(CMAKE_C_FLAGS "-Wall -Wextra -pthread -fdiagnostics-color=always")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14")

set(M3_CROSS false CACHE BOOLEAN "Cross compile using m3-toolchain")

if(M3_CROSS)
  set(CMAKE_C_COMPILER "${CMAKE_SOURCE_DIR}/m3-toolchain/bin/arm-cortexa9_neon-linux-gnueabi-gcc")
  set(CMAKE_CXX_COMPILER "${CMAKE_SOURCE_DIR}/m3-toolchain/bin/arm-cortexa9_neon-linux-gnueabi-g++")
  set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SOURCE_DIR}/m3-toolchain/arm-cortexa9_neon-linux-gnueabi/sysroot")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DM3_CROSS")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DM3_CROSS")
  set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
else()
  set(CMAKE_C_COMPILER "clang-3.6")
  set(CMAKE_CXX_COMPILER "clang++-3.6")
  # Ubuntu 14.10's libstdc++ is broken in C++14.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()

find_package(PkgConfig)
pkg_search_module(LIBUSB REQUIRED libusb-1.0)
if(NOT M3_CROSS)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)
pkg_search_module(WAYLAND_CLIENT wayland-client)
endif()

link_directories(
  ${LIBUSB_LIBRARY_DIRS}
  ${GSTREAMER_LIBRARY_DIRS}
  ${WAYLAND_CLIENT_LIBRARY_DIRS}
)

include_directories(
  ${LIBUSB_INCLUDE_DIRS}
  ${GSTREAMER_INCLUDE_DIRS}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

add_executable(
  mimic
  src/aoa.cpp
  src/main_gst.cpp
)

target_link_libraries(
  mimic
  pthread
  ${LIBUSB_LIBRARIES}
)

if(NOT M3_CROSS)
add_executable(
  mimic_wayland
  src/aoa.cpp
  src/decoder.cpp
  src/main_wayland.cpp
)

target_link_libraries(
  mimic_wayland
  pthread
  ${LIBUSB_LIBRARIES}
  ${GSTREAMER_LIBRARIES}
  gstapp-1.0
  gstvideo-1.0
  ${WAYLAND_CLIENT_LIBRARIES}
)
endif()
