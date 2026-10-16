// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mutex>

// QIcon's theme lookups share caches that have no lock of their own, and image://icons runs on
// Qt Quick's loading thread for an asynchronous Image (the Launchpad's): every theme lookup or
// change of theme holds this lock.
inline std::mutex &iconThemeLock() {
    static std::mutex lock;
    return lock;
}
