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
#include <format>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>

#include <cx/core/log/journal_entry.hpp>
#include <cx/core/serialization/text_escape.hpp>

namespace cx::core::log {

inline const char *to_string(journal_level level) {
  switch (level) {
  case journal_level::info:
    return "info";
  case journal_level::warn:
    return "warn";
  case journal_level::debug:
    return "debug";
  case journal_level::error:
    return "error";
  }
  return "unknown";
}

namespace detail {

inline std::string format_timestamp(const std::chrono::system_clock::time_point &timestamp) {
  return std::format("{:%FT%T}", std::chrono::floor<std::chrono::milliseconds>(timestamp));
}

inline std::string format_thread_id(const std::thread::id &id) { return std::format("{}", id); }

} // namespace detail

// SRS-006 §6.2: a rendering strategy streamed onto a journal_stream the way
// std::hex streams onto std::cout - selecting one doesn't touch journal or
// journal_entry, so new output formats never require a journal.hpp change.
class journal_serializer {
public:
  virtual ~journal_serializer() = default;
  virtual void write(const journal_entry &entry, std::ostream &out) const = 0;
};

class plain_journal_serializer : public journal_serializer {
public:
  void write(const journal_entry &entry, std::ostream &out) const override {
    out << detail::format_timestamp(entry.timestamp) << " [" << to_string(entry.level) << "] "
        << (entry.file ? entry.file : "?") << ":" << entry.line << " " << (entry.function ? entry.function : "?")
        << " (thread " << detail::format_thread_id(entry.thread_id) << ") " << entry.message << '\n';
  }
};

class json_journal_serializer : public journal_serializer {
public:
  void write(const journal_entry &entry, std::ostream &out) const override {
    out << R"({"timestamp":")" << detail::format_timestamp(entry.timestamp) << R"(","level":")"
        << to_string(entry.level) << R"(","file":")";
    serialization::write_json_escaped(out, entry.file ? entry.file : "");
    out << R"(","line":)" << entry.line << R"(,"function":")";
    serialization::write_json_escaped(out, entry.function ? entry.function : "");
    out << R"(","message":")";
    serialization::write_json_escaped(out, entry.message);
    out << R"(","thread_id":")" << detail::format_thread_id(entry.thread_id) << "\"}\n";
  }
};

class xml_journal_serializer : public journal_serializer {
public:
  void write(const journal_entry &entry, std::ostream &out) const override {
    out << "<journal_entry timestamp=\"" << detail::format_timestamp(entry.timestamp) << "\" level=\""
        << to_string(entry.level) << "\" file=\"";
    serialization::write_xml_escaped(out, entry.file ? entry.file : "");
    out << "\" line=\"" << entry.line << "\" function=\"";
    serialization::write_xml_escaped(out, entry.function ? entry.function : "");
    out << "\" thread_id=\"" << detail::format_thread_id(entry.thread_id) << "\">";
    serialization::write_xml_escaped(out, entry.message);
    out << "</journal_entry>\n";
  }
};

class csv_journal_serializer : public journal_serializer {
public:
  void write(const journal_entry &entry, std::ostream &out) const override {
    serialization::write_csv_field(out, detail::format_timestamp(entry.timestamp));
    out << ',';
    serialization::write_csv_field(out, to_string(entry.level));
    out << ',';
    serialization::write_csv_field(out, entry.file ? entry.file : "");
    out << ',';
    out << entry.line << ',';
    serialization::write_csv_field(out, entry.function ? entry.function : "");
    out << ',';
    serialization::write_csv_field(out, detail::format_thread_id(entry.thread_id));
    out << ',';
    serialization::write_csv_field(out, entry.message);
    out << '\n';
  }
};

namespace detail {

// Which journal_serializer is "selected" on a given std::ostream, so
// `out << serializer` sticks for every journal_entry streamed onto that same
// ostream afterwards (SRS-006 §3/§6.2) - the same sticky-manipulator
// behavior std::hex relies on, but kept in an ordinary guarded map keyed by
// ostream* rather than via ios_base::pword/xalloc, since journal's sinks
// (stdout, a single log file) are process-lifetime, so there is nothing to
// reclaim on stream destruction that would matter in practice.
class serializer_registry {
public:
  static serializer_registry &instance() {
    static serializer_registry registry;
    return registry;
  }

  void select(std::ostream &out, std::shared_ptr<journal_serializer> serializer) {
    std::unique_lock lock(mutex_);
    selected_[&out] = std::move(serializer);
  }

  std::shared_ptr<journal_serializer> selected(std::ostream &out) {
    std::unique_lock lock(mutex_);
    auto it = selected_.find(&out);
    return it == selected_.end() ? nullptr : it->second;
  }

private:
  std::mutex mutex_;
  std::unordered_map<std::ostream *, std::shared_ptr<journal_serializer>> selected_;
};

} // namespace detail

// Constrained on T so this outranks std::shared_ptr's own unconstrained
// operator<< (which would otherwise win overload resolution outright and
// print the pointer's address instead of selecting it - a constrained
// template is more specialized than an unconstrained one with an otherwise
// identical signature, per the partial-ordering rules).
template <typename serializer_type>
  requires std::derived_from<serializer_type, journal_serializer>
std::ostream &operator<<(std::ostream &out, std::shared_ptr<serializer_type> serializer) {
  detail::serializer_registry::instance().select(out, std::move(serializer));
  return out;
}

inline std::ostream &operator<<(std::ostream &out, const journal_entry &entry) {
  // journal defaults to plain_journal_serializer (SRS-006 §6.2) so an
  // ostream nobody has streamed a serializer onto yet still renders instead
  // of silently doing nothing.
  static const plain_journal_serializer default_serializer;
  auto selected = detail::serializer_registry::instance().selected(out);
  const journal_serializer &active = selected ? *selected : static_cast<const journal_serializer &>(default_serializer);
  active.write(entry, out);
  return out;
}

} // namespace cx::core::log
