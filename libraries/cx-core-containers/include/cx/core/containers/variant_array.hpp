// ---------------------------------------------------------------------------
// PROPRIETARY CODE – Arthur de Araújo Farias 2025
// All rights reserved.  No part of this file may be reproduced, stored in a
// retrieval system, or transmitted in any form or by any means—electronic,
// mechanical, photocopying, recording, or otherwise—without the prior written
// permission of the copyright holder.
// ---------------------------------------------------------------------------

#pragma once

#include <deque>

#include <cx/core/containers/variant.hpp>

namespace cx::core::containers {

using variant_array = std::deque<variant>;

template <class Archiver> void archive(Archiver &ar, variant_array &value) { ar % serialization::tags::v{value}; }
template <class Archiver> void archive(Archiver &ar, const variant_array &value) { ar % serialization::tags::v{value}; }

} // namespace cx::core::containers
