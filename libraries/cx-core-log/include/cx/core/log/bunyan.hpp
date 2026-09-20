// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <unistd.h>

#include <cx/core/serialization/text_escape.hpp>

// One log record as one line of Bunyan-format JSON
// (https://github.com/trentm/node-bunyan#log-record-fields): the fixed fields
// every reader relies on — v, level, name, hostname, pid, time, msg — then the
// caller's own. Anything that reads Bunyan reads this: the `bunyan` CLI, a log
// shipper, or `agenticx-ncortex logview`. Brought in from agenticx-ncortex;
// the journal beside it (journal.hpp) is cx-flow's in-process record, this is the
// line a process writes for other programs to read.
//
// Structured logging: `bunyan::info() << "epoch done" << bunyan::field("loss", 0.42);`
// writes one Bunyan JSON line to stderr when the statement ends. Text pieces
// concatenate into `msg`; fields become members of the record, with their JSON
// type kept (a number stays a number, so a reader can plot it).
//
// stdout is a program's result and stays clean; logs are stderr. Set
// CX_LOG_LEVEL (trace|debug|info|warn|error|fatal) to choose what is written;
// info is the default. A program with its own variable and logger name sets
// configuration().minimum / .name once at start-up (agenticx-ncortex does).
namespace cx::core::log::bunyan {

// Bunyan's numeric levels.
enum class level : int { trace = 10, debug = 20, info = 30, warn = 40, error = 50, fatal = 60 };

inline std::string escape(std::string_view text) { return serialization::json_escaped(text); }

// UTC, ISO 8601 with milliseconds — the form Bunyan writes.
inline std::string timestamp(std::chrono::system_clock::time_point at = std::chrono::system_clock::now()) {
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count() % 1000;
  const std::time_t seconds = std::chrono::system_clock::to_time_t(at);
  std::tm utc{};
  ::gmtime_r(&seconds, &utc);
  char buffer[40];
  std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900, utc.tm_mon + 1,
                utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(milliseconds));
  return buffer;
}

inline std::string hostname() {
  char buffer[256] = {};
  return ::gethostname(buffer, sizeof buffer - 1) == 0 ? buffer : "localhost";
}

// `fields` is already JSON: zero or more `,"key":value` members.
inline std::string format(level severity, std::string_view name, std::string_view message, std::string_view fields) {
  return "{\"v\":0,\"level\":" + std::to_string(static_cast<int>(severity)) + ",\"name\":\"" + escape(name) +
         "\",\"hostname\":\"" + escape(hostname()) + "\",\"pid\":" + std::to_string(::getpid()) + ",\"time\":\"" +
         timestamp() + "\",\"msg\":\"" + escape(message) + "\"" + std::string(fields) + "}";
}

// A key and its value, rendered to JSON at construction.
struct field {
  field(std::string_view key, std::string_view value) : json(member(key) + "\"" + escape(value) + "\"") {}
  field(std::string_view key, const char *value) : field(key, std::string_view(value)) {}
  field(std::string_view key, const std::string &value) : field(key, std::string_view(value)) {}
  field(std::string_view key, bool value) : json(member(key) + (value ? "true" : "false")) {}
  template <typename number>
    requires(std::is_arithmetic_v<number> && !std::same_as<number, bool>)
  field(std::string_view key, number value) : json(member(key) + render(value)) {}

  std::string json;

private:
  static std::string member(std::string_view key) { return ",\"" + escape(key) + "\":"; }
  template <typename number> static std::string render(number value) {
    if constexpr (std::is_floating_point_v<number>) {
      char buffer[40];
      // JSON has no NaN or infinity; a reader is better served by null than by a line it cannot parse.
      if (value != value || value - value != 0)
        return "null";
      std::snprintf(buffer, sizeof buffer, "%.9g", static_cast<double>(value));
      return buffer;
    } else {
      return std::to_string(value);
    }
  }
};

struct settings {
  std::string name = "cx";
  level minimum = level::info;
  std::FILE *sink = stderr;
};

inline level parse_level(std::string_view text, level fallback) {
  for (const auto &[name, value] : {std::pair{"trace", level::trace}, {"debug", level::debug}, {"info", level::info},
                                    {"warn", level::warn}, {"error", level::error}, {"fatal", level::fatal}})
    if (text == name)
      return value;
  return fallback;
}

// Process-wide. A leaf sets `name` to its own once, at start-up.
inline settings &configuration() {
  static settings current = [] {
    settings initial;
    if (const char *minimum = std::getenv("CX_LOG_LEVEL"))
      initial.minimum = parse_level(minimum, level::info);
    return initial;
  }();
  return current;
}

// One record in the making; written when it goes out of scope, which for the
// usual temporary is the end of the statement.
struct record {
  explicit record(level severity) : severity_(severity) {}
  record(const record &) = delete;
  record &operator=(const record &) = delete;

  ~record() {
    const settings &current = configuration();
    if (severity_ < current.minimum || current.sink == nullptr)
      return;
    const std::string line = format(severity_, current.name, message_, fields_) + "\n";
    std::fwrite(line.data(), 1, line.size(), current.sink); // one write per record keeps lines whole
    std::fflush(current.sink);
  }

  record &operator<<(std::string_view text) {
    message_ += text;
    return *this;
  }
  record &operator<<(const field &extra) {
    fields_ += extra.json;
    return *this;
  }
  template <typename number>
    requires std::is_arithmetic_v<number>
  record &operator<<(number value) {
    message_ += std::to_string(value);
    return *this;
  }

private:
  level severity_;
  std::string message_, fields_;
};

inline record trace() { return record(level::trace); }
inline record debug() { return record(level::debug); }
inline record info() { return record(level::info); }
inline record warn() { return record(level::warn); }
inline record error() { return record(level::error); }
inline record fatal() { return record(level::fatal); }

} // namespace cx::core::log::bunyan
