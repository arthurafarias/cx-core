// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <source_location>
#include <thread>
#include <type_traits>
#include <utility>

#include <cx/core/log/journal_entry.hpp>
#include <cx/core/log/journal_serializer.hpp>
#include <cx/core/log/journal_stream.hpp>
#include <cx/core/signals/signal.hpp>
#include <cx/core/threading/thread_pool.hpp>

namespace cx::core::log {

// Binds a std::format compile-time-checked format string together with the
// call site's source_location, captured via a defaulted *constructor*
// argument. A plain trailing source_location function parameter can't
// coexist with a deduced args_types&&... pack (the pack greedily absorbs
// every positional argument), so the location instead rides along on the
// format-string argument itself - the same trick std::source_location's own
// "hidden default parameter" idiom relies on.
template <typename... args_types> struct journal_format_loc {
  std::format_string<args_types...> fmt;
  std::source_location where;

  template <typename fmt_type>
  consteval journal_format_loc(const fmt_type &f, std::source_location loc = std::source_location::current())
      : fmt(f), where(loc) {}
};

// SRS-006 §6.3: non-copyable, non-movable façade. §7.1 forbids holding a
// journal (or any logging object) as a member of a core-library element -
// call cxflow::journal::info/warn/debug/error(...) directly at the call
// site instead. Dispatch reuses cxflow::threading::signal rather than a
// bespoke queue (§7.2), so set_serializer() just (re)wires the one slot
// that renders each journal_entry through whichever journal_serializer is
// currently selected.
class journal {
public:
  journal() = delete;

  static void level_set(journal_level level) { min_level().store(level); }
  static journal_level level() { return min_level().load(); }

  static void set_serializer(std::shared_ptr<journal_serializer> serializer) {
    dispatcher().disconnect_all();
    dispatcher().connect([serializer](const journal_entry &entry) { stream() << serializer << entry; });
  }

  template <typename... args_types>
  static void info(journal_format_loc<std::type_identity_t<args_types>...> fmt, args_types &&...args) {
    emit_if(journal_level::info, fmt, std::forward<args_types>(args)...);
  }

  template <typename... args_types>
  static void warn(journal_format_loc<std::type_identity_t<args_types>...> fmt, args_types &&...args) {
    emit_if(journal_level::warn, fmt, std::forward<args_types>(args)...);
  }

  template <typename... args_types>
  static void debug(journal_format_loc<std::type_identity_t<args_types>...> fmt, args_types &&...args) {
    emit_if(journal_level::debug, fmt, std::forward<args_types>(args)...);
  }

  // error is never gated by level_set() (SRS-006 §6.3/§3).
  template <typename... args_types>
  static void error(journal_format_loc<std::type_identity_t<args_types>...> fmt, args_types &&...args) {
    dispatcher().emit(make_entry(journal_level::error, fmt.where, fmt.fmt, std::forward<args_types>(args)...));
  }

  // Deferred dispatch through a thread_pool (SRS-006 §7.2 "no overhead"
  // policy / §8 - priority-aware submission landed in SRS-007). Callers on
  // a hot path should pass threading::task_priority::low so logging can
  // never delay media events queued behind it.
  template <typename... args_types>
  static void emit_async(threading::thread_pool &pool, threading::task_priority priority, journal_level level,
                          journal_format_loc<std::type_identity_t<args_types>...> fmt, args_types &&...args) {
    if (level != journal_level::error && min_level().load() < level) return;
    dispatcher().emit_async(pool, priority,
                             make_entry(level, fmt.where, fmt.fmt, std::forward<args_types>(args)...));
  }

private:
  template <typename... args_types>
  static void emit_if(journal_level level, journal_format_loc<std::type_identity_t<args_types>...> fmt,
                       args_types &&...args) {
    // Filtered out before the journal_entry (and its formatted message) is
    // ever built, per the journal definition (SRS-006 §3).
    if (min_level().load() < level) return;
    dispatcher().emit(make_entry(level, fmt.where, fmt.fmt, std::forward<args_types>(args)...));
  }

  template <typename... args_types>
  static journal_entry make_entry(journal_level level, const std::source_location &where,
                                   std::format_string<args_types...> fmt, args_types &&...args) {
    return journal_entry{
        .timestamp = std::chrono::system_clock::now(),
        .level = level,
        .line = where.line(),
        .file = where.file_name(),
        .function = where.function_name(),
        .message = std::format(fmt, std::forward<args_types>(args)...),
        .thread_id = std::this_thread::get_id(),
    };
  }

  // min_level defaults to journal_level::debug - the most permissive
  // setting (nothing is filtered) so a fresh process is non-breaking by
  // default (SRS-006 §6.3). level_set() progressively silences debug, then
  // warn, as it is tightened; error is never gated.
  static std::atomic<journal_level> &min_level() {
    static std::atomic<journal_level> level{journal_level::debug};
    return level;
  }

  // By value, not by reference: signal::emit_async() captures args into a
  // deferred pool task, which is unsound for reference-typed args (the
  // referent may not outlive the call) and is statically rejected at
  // instantiation for those - see threading::signal::emit_async.
  //
  // Starts with one slot already connected, streaming straight through
  // stream() (which falls back to plain_journal_serializer itself when
  // nothing has been selected on that ostream, see journal_serializer.hpp) -
  // without it, an untouched journal would silently drop every entry until
  // some caller happened to invoke set_serializer() first, contradicting
  // "journal defaults to plain_journal_serializer... non-breaking for
  // existing callers" (§6.2). set_serializer() replaces this default slot
  // the same way it replaces any other (disconnect_all() then connect()).
  // The two static locals are guaranteed to initialize in declaration order
  // ([basic.start.dynamic]), so connect() always runs against a fully
  // constructed sig.
  static signals::signal<journal_entry> &dispatcher() {
    static signals::signal<journal_entry> sig;
    static bool default_slot_connected =
        (sig.connect([](const journal_entry &entry) { stream() << entry; }), true);
    (void)default_slot_connected;
    return sig;
  }
};

} // namespace cx::core::log
