cmake_minimum_required(VERSION 2.8.9)
project(mimic)

find_package(PkgConfig)
pkg_search_module(LIBUSB REQUIRED libusb-1.0)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)

set(CMAKE_C_COMPILER "clang-3.6")
set(CMAKE_CXX_COMPILER "clang++-3.6")
set(CMAKE_C_FLAGS "-Wall -Wextra -pthread -fdiagnostics-color=always")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14 -stdlib=libc++")

link_directories(
  ${LIBUSB_LIBRARY_DIRS}
  ${GSTREAMER_INCLUDE_DIRS}
)

include_directories(
  ${LIBUSB_INCLUDE_DIRS}
  ${GSTREAMER_INCLUDE_DIRS}
)

add_executable(
  mimic
  src/aoa.cpp
  src/main.cpp
)

target_link_libraries(
  mimic
  ${LIBUSB_LIBRARIES}
  ${GSTREAMER_LIBRARIES}
  c++
  pthread
)
