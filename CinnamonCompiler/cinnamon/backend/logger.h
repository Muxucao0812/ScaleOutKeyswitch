// Copyright (c) Siddharth Jayashankar. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <fmt/core.h>
namespace Cinnamon {
namespace Backend {

template <typename... Args>
void log(const char *file, uint64_t line, const char *fmt, Args... args) {
  if (0) {
    fmt::print("LOG {}:{}: ", file, line);
    fmt::print(fmt, args...);
    // printf("LOG %lu: ",line);
    // va_list args;
    // va_start(args, fmt);
    // vprintf(fmt, args);
    // va_end(args);
    // printf("\n");
    fmt::print("\n");
    fflush(stdout);
  }
}

template <typename... Args>
void log2(const char *file, uint64_t line, const char *fmt, Args... args) {
  fmt::print("LOG {}:{}: ", file, line);
  fmt::print(fmt, args...);
  // printf("LOG %lu: ",line);
  // va_list args;
  // va_start(args, fmt);
  // vprintf(fmt, args);
  // va_end(args);
  // printf("\n");
  fmt::print("\n");
  fflush(stdout);
}

#define CL_LOG(format, args...)                                                \
  do {                                                                         \
    Cinnamon::Backend::log(__FILE__, __LINE__, format, args);                  \
  } while (0);
#define CL_LOG2(format, args...)                                               \
  do {                                                                         \
    Cinnamon::Backend::log2(__FILE__, __LINE__, format, args);                 \
  } while (0);

bool get_boolean_env_variable(const std::string &var) {
  if (const char *env_p = std::getenv(var.c_str())) {
    auto env_str = std::string(env_p);
    if (env_str == "1") {
      return true;
    } else {
      return false;
    }
  }
  return false;
}

} // namespace Backend
} // namespace Cinnamon