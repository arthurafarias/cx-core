// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <string>
#include <thread>

namespace cx::core::log {

// SRS-006 §3: relative ordering here doubles as the level_set() gate order
// (see journal.hpp) - error is intentionally last and never gated.
enum class journal_level { info = 0, warn = 1, debug = 2, error = 3 };

// SRS-006 §6.1: the immutable value object dispatched for one log event.
// journal builds one of these per call and hands it to
// cxflow::threading::signal - nothing here is mutated after construction.
struct journal_entry {
  std::chrono::system_clock::time_point timestamp;
  journal_level level = journal_level::info;
  unsigned int line = 0;
  const char *file = nullptr;
  const char *function = nullptr;
  std::string message;
  std::thread::id thread_id;
};

} // namespace cx::core::log
