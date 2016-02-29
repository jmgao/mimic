#pragma once

#include <stdio.h>

#define _log(fmt, prefix, ...) fprintf(stderr, prefix fmt "\n", ##__VA_ARGS__)
#define log(fmt, ...) _log(fmt, "", ##__VA_ARGS__)
#define debug(fmt, ...) _log(fmt, "debug: ", ##__VA_ARGS__)
#define info(fmt, ...) _log(fmt, "info: ", ##__VA_ARGS__)
#define warn(fmt, ...) _log(fmt, "warning: ", ##__VA_ARGS__)
#define error(fmt, ...) _log(fmt, "error: ", ##__VA_ARGS__)
#define fatal(fmt, ...)       \
  do {                        \
    error(fmt, ##__VA_ARGS__); \
    exit(1);                  \
  } while (0)
