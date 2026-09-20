// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <iostream>
#include <ostream>

namespace cx::core::log {

// SRS-006 §3: the destination journal_serializer/journal_entry values are
// streamed onto (stdout, file, syslog, ...). Persistence/rotation are the
// backing std::ostream's own concern - journal_stream only tracks which one
// is currently selected, defaulting to std::cout so a fresh process needs no
// setup to see log output.
class journal_stream {
public:
  static void set(std::ostream &out) { target() = &out; }
  static std::ostream &get() { return *target(); }

private:
  static std::ostream *&target() {
    static std::ostream *out = &std::cout;
    return out;
  }
};

// SRS-006 §6.3 reference code streams through a free `stream()` call
// (`stream() << serializer << entry`) - this is that call.
inline std::ostream &stream() { return journal_stream::get(); }

} // namespace cx::core::log
