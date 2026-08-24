#include "core/ids.h"

#include <atomic>
#include <random>

namespace core {

Id new_id() {
    // Seeded once from the OS entropy source; a counter in the low bits makes
    // exhaustion of the generator's period harmless within one session.
    static std::atomic<std::uint64_t> counter{1};
    thread_local std::mt19937_64 rng = [] {
        std::random_device rd;
        return std::mt19937_64{(static_cast<std::uint64_t>(rd()) << 32) ^ rd()};
    }();
    return Id{rng(), rng() ^ counter.fetch_add(1, std::memory_order_relaxed)};
}

std::string to_string(const Id& id) {
    char buf[36];
    std::snprintf(buf, sizeof buf, "%016llx%016llx",
                  static_cast<unsigned long long>(id.hi),
                  static_cast<unsigned long long>(id.lo));
    return buf;
}

}  // namespace core
